/*
goat3d - 3D scene, and animation file format library.
Copyright (C) 2013-2018  John Tsiombikas <nuclear@member.fsf.org>

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU Lesser General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU Lesser General Public License for more details.

You should have received a copy of the GNU Lesser General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/
#include <string.h>
#include <errno.h>
#include <ctype.h>
#include <string>
#include "goat3d.h"
#include "goat3d_impl.h"
#include "log.h"

#ifndef _MSC_VER
#include <alloca.h>
#else
#include <malloc.h>
#endif


static long read_file(void *buf, size_t bytes, void *uptr);
static long write_file(const void *buf, size_t bytes, void *uptr);
static long seek_file(long offs, int whence, void *uptr);

GOAT3DAPI struct goat3d *goat3d_create(void)
{
	struct goat3d *g;

	if(!(g = malloc(sizeof *g))) {
		return 0;
	}
	if(goat3d_init(g) == -1) {
		free(g);
		return 0;
	}
	return goat;
}

GOAT3DAPI void goat3d_free(struct goat3d *g)
{
	goat3d_destroy(g);
	free(g);
}

int goat3d_init(struct goat3d *g)
{
	memset(g, 0, sizeof *g);

	if(goat3d_set_name(g, "unnamed") == -1) goto err;
	cgm_vcons(&g->ambient, 0.05, 0.05, 0.05);

	if(!(g->materials = dynarr_alloc(0, sizeof *g->materials))) goto err;
	if(!(g->meshes = dynarr_alloc(0, sizeof *g->meshes))) goto err;
	if(!(g->lights = dynarr_alloc(0, sizeof *g->lights))) goto err;
	if(!(g->cameras = dynarr_alloc(0, sizeof *g->cameras))) goto err;
	if(!(g->nodes = dynarr_alloc(0, sizeof *g->nodes))) goto err;

	goat3d_setopt(g, GOAT3D_OPT_SAVEXML, 1);
	return 0;

err:
	goat3d_destroy(g);
	return -1;
}

void goat3d_destroy(struct goat3d *g)
{
	goat3d_clear(g);

	dynarr_free(g->materials);
	dynarr_free(g->meshes);
	dynarr_free(g->lights);
	dynarr_free(g->cameras);
	dynarr_free(g->nodes);
}

void goat3d_clear(struct goat3d *g)
{
	int i, j, num;

	num = dynarr_size(g->materials);
	for(i=0; i<num; i++) {
		free(g->materials[i].name);
		for(j=0; j<dynarr_size(g->materials[i].attrib); j++) {
			free(g->materials[i].attrib[j].name);
			free(g->materials[i].attrib[j].map);
		}
		dynarr_free(g->materials[i].attrib);
	}
	dynarr_clear(g->materials);

	/* TODO cont. clear */

	goat3d_set_name(g, "unnamed");
}

GOAT3DAPI void goat3d_setopt(struct goat3d *g, enum goat3d_option opt, int val)
{
	if(val) {
		g->flags |= (1 << (int)opt);
	} else {
		g->flags &= ~(1 << (int)opt);
	}
}

GOAT3DAPI int goat3d_getopt(const struct goat3d *g, enum goat3d_option opt)
{
	return (g->flags >> (int)opt) & 1;
}

GOAT3DAPI int goat3d_load(struct goat3d *g, const char *fname)
{
	FILE *fp = fopen(fname, "rb");
	if(!fp) {
		logmsg(LOG_ERROR, "failed to open file \"%s\" for reading: %s\n", fname, strerror(errno));
		return -1;
	}

	/* if the filename contained any directory components, keep the prefix
	 * to use it as a search path for external mesh file loading
	 */
	g->search_path = new char[strlen(fname) + 1];
	strcpy(g->search_path, fname);

	char *slash = strrchr(g->search_path, '/');
	if(slash) {
		*slash = 0;
	} else {
		if((slash = strrchr(g->search_path, '\\'))) {
			*slash = 0;
		} else {
			delete [] g->search_path;
			g->search_path = 0;
		}
	}

	int res = goat3d_load_file(g, fp);
	fclose(fp);
	return res;
}

GOAT3DAPI int goat3d_save(const struct goat3d *g, const char *fname)
{
	FILE *fp = fopen(fname, "wb");
	if(!fp) {
		logmsg(LOG_ERROR, "failed to open file \"%s\" for writing: %s\n", fname, strerror(errno));
		return -1;
	}

	int res = goat3d_save_file(g, fp);
	fclose(fp);
	return res;
}

GOAT3DAPI int goat3d_load_file(struct goat3d *g, FILE *fp)
{
	struct goat3d_io io;
	io.cls = fp;
	io.read = read_file;
	io.write = write_file;
	io.seek = seek_file;

	return goat3d_load_io(g, &io);
}

GOAT3DAPI int goat3d_save_file(const struct goat3d *g, FILE *fp)
{
	struct goat3d_io io;
	io.cls = fp;
	io.read = read_file;
	io.write = write_file;
	io.seek = seek_file;

	return goat3d_save_io(g, &io);
}

GOAT3DAPI int goat3d_load_io(struct goat3d *g, struct goat3d_io *io)
{
	if(g3dimpl_scnload_bin(g, io)) {
		if(!g3dimpl_scnload_text(g, io)) {
			return -1;
		}
	}
	return 0;
}

GOAT3DAPI int goat3d_save_io(const struct goat3d *g, struct goat3d_io *io)
{
	if(goat3d_getopt(g, GOAT3D_OPT_SAVEXML)) {
		goat3d_logmsg(LOG_ERROR, "saving in the original xml format is no longer supported\n");
		return -1;
	} else if(goat3d_getopt(g, GOAT3D_OPT_SAVETEXT)) {
		return g3dimpl_scnsave_text(g, io);
	}
	return g3dimpl_scnsave_bin(g, io);
}

/* save/load animations */
GOAT3DAPI int goat3d_load_anim(struct goat3d *g, const char *fname)
{
	int res;
	FILE *fp;

	if(!(fp = fopen(fname, "rb"))) {
		return -1;
	}

	res = goat3d_load_anim_file(g, fp);
	fclose(fp);
	return res;
}

GOAT3DAPI int goat3d_save_anim(const struct goat3d *g, const char *fname)
{
	int res;
	FILE *fp;

	if(!(fp = fopen(fname, "wb"))) {
		return -1;
	}

	res = goat3d_save_anim_file(g, fp);
	fclose(fp);
	return res;
}

GOAT3DAPI int goat3d_load_anim_file(struct goat3d *g, FILE *fp)
{
	struct goat3d_io io;
	io.cls = fp;
	io.read = read_file;
	io.write = write_file;
	io.seek = seek_file;

	return goat3d_load_anim_io(g, &io);
}

GOAT3DAPI int goat3d_save_anim_file(const struct goat3d *g, FILE *fp)
{
	struct goat3d_io io;
	io.cls = fp;
	io.read = read_file;
	io.write = write_file;
	io.seek = seek_file;

	return goat3d_save_anim_io(g, &io);
}

GOAT3DAPI int goat3d_load_anim_io(struct goat3d *g, struct goat3d_io *io)
{
	if(g3dimpl_anmload_bin(g, io) == -1) {
		if(g3dimpl_anmload_text(g, io) == -1) {
			return -1;
		}
	}
	return 0;
}

GOAT3DAPI int goat3d_save_anim_io(const struct goat3d *g, struct goat3d_io *io)
{
	if(goat3d_getopt(g, GOAT3D_OPT_SAVEXML)) {
		goat3d_logmsg(LOG_ERROR, "saving in the original xml format is no longer supported\n");
		return -1;
	} else if(goat3d_getopt(g, GOAT3D_OPT_SAVETEXT)) {
		return g3dimpl_anmsave_text(io);
	}
	return g3dimpl_anmsave_bin(io);
}


GOAT3DAPI int goat3d_set_name(struct goat3d *g, const char *name)
{
	int len = strlen(name);

	free(g->name);
	if(!(g->name = malloc(len + 1))) {
		return -1;
	}
	memcpy(g->name, name, len + 1);
	return 0;
}

GOAT3DAPI const char *goat3d_get_name(const struct goat3d *g)
{
	return g->name;
}

GOAT3DAPI void goat3d_set_ambient(struct goat3d *g, const float *amb)
{
	cgm_vcons(&g->ambient, amb[0], amb[1], amb[2]);
}

GOAT3DAPI void goat3d_set_ambient3f(struct goat3d *g, float ar, float ag, float ab)
{
	cgm_vcons(&g->ambient, ar, ag, ab);
}

GOAT3DAPI const float *goat3d_get_ambient(const struct goat3d *g)
{
	return &g->ambient.x;
}

GOAT3DAPI int goat3d_get_bounds(const struct goat3d *g, float *bmin, float *bmax)
{
	int i, num_nodes;
	struct aabox node_bbox;

	if(!g->bbox_valid) {
		g3dimpl_aabox_init(&g->bbox);

		num_nodes = dynarr_size(g->nodes);
		for(i=0; i<num_nodes; i++) {
			if(g->nodes[i]->parent) {
				continue;
			}
			g3dimpl_node_bounds(&node_bbox, g->nodes[i]);
			g3dimpl_aabox_union(&g->bbox, &g->bbox, &node_bbox);
		}
		g->bbox_valid = 1;
	}

	bmin[0] = g->bbox.min.x;
	bmin[1] = g->bbox.min.y;
	bmin[2] = g->bbox.min.z;
	bmax[0] = g->bbox.max.x;
	bmax[1] = g->bbox.max.y;
	bmax[2] = g->bbox.max.z;
	return 0;
}

// ---- materials ----
GOAT3DAPI int goat3d_add_mtl(struct goat3d *g, struct goat3d_material *mtl)
{
	struct material *newarr;
	if(!(newarr = dynarr_push(g->materials, &mtl))) {
		return -1;
	}
	g->materials = newarr;
	return 0;
}

GOAT3DAPI int goat3d_get_mtl_count(struct goat3d *g)
{
	return dynarr_size(g->materials);
}

GOAT3DAPI struct goat3d_material *goat3d_get_mtl(struct goat3d *g, int idx)
{
	return g->materials[idx];
}

GOAT3DAPI struct goat3d_material *goat3d_get_mtl_by_name(struct goat3d *g, const char *name)
{
	int i, num = dynarr_size(g->materials);
	for(i=0; i<num; i++) {
		if(strcmp(g->materials[i]->name, name) == 0) {
			return g->materials[i];
		}
	}
	return 0;
}

GOAT3DAPI struct goat3d_material *goat3d_create_mtl(void)
{
	struct goat3d_material *mtl;
	if(!(mtl = malloc(sizeof *mtl))) {
		return 0;
	}
	g3dimpl_mtl_init(mtl);
	return mtl;
}

GOAT3DAPI void goat3d_destroy_mtl(struct goat3d_material *mtl)
{
	g3dimpl_mtl_destroy(mtl);
	free(mtl);
}

GOAT3DAPI int goat3d_set_mtl_name(struct goat3d_material *mtl, const char *name)
{
	char *tmp;
	int len = strlen(name);
	if(!(tmp = malloc(len + 1))) {
		return -1;
	}
	memcpy(tmp, name, len + 1);
	free(mtl->name);
	mtl->name = tmp;
	return 0;
}

GOAT3DAPI const char *goat3d_get_mtl_name(const struct goat3d_material *mtl)
{
	return mtl->name;
}

GOAT3DAPI int goat3d_set_mtl_attrib(struct goat3d_material *mtl, const char *attrib, const float *val)
{
	struct material_attrib *ma = g3dimpl_mtl_getattr(mtl, attr);
	if(!ma) return -1;
	cgm_vcons(&ma->value, val[0], val[1], val[2], val[3]);
	return 0;
}

GOAT3DAPI int goat3d_set_mtl_attrib1f(struct goat3d_material *mtl, const char *attrib, float val)
{
	return goat3d_set_mtl_attrib4f(mtl, attrib, val, 0, 0, 1);
}

GOAT3DAPI int goat3d_set_mtl_attrib3f(struct goat3d_material *mtl, const char *attrib, float r, float g, float b)
{
	return goat3d_set_mtl_attrib4f(mtl, attrib, r, g, b, 1);
}

GOAT3DAPI int goat3d_set_mtl_attrib4f(struct goat3d_material *mtl, const char *attrib, float r, float g, float b, float a)
{
	struct material_attrib *ma = g3dimpl_mtl_getattr(mtl, attr);
	if(!ma) return -1;
	cgm_vcons(&ma->value, r, g, b, a);
	return 0;
}

GOAT3DAPI const float *goat3d_get_mtl_attrib(struct goat3d_material *mtl, const char *attrib)
{
	struct material_attrib *ma = g3dimpl_mtl_findattr(mtl, attr);
	return ma ? &ma->value.x : 0;
}

GOAT3DAPI int goat3d_set_mtl_attrib_map(struct goat3d_material *mtl, const char *attrib, const char *mapname)
{
	int len;
	char *tmp;
	struct material_attrib *ma;

	len = strlen(mapname);
	if(!(tmp = malloc(len + 1))) {
		return -1;
	}
	memcpy(tmp, mapname, len + 1);

	if(!(ma = g3dimpl_mtl_getattr(mtl, attr))) {
		free(tmp);
		return -1;
	}
	free(ma->map);
	ma->map = tmp;
	tmp = clean_filename(ma->map);
	if(tmp != ma->map) {
		memmove(ma->map, tmp, len - (tmp - ma->map) + 1);
	}
	return 0;
}

GOAT3DAPI const char *goat3d_get_mtl_attrib_map(struct goat3d_material *mtl, const char *attrib)
{
	struct material_attrib *ma = g3dimpl_mtl_findattr(mtl, attr);
	return ma->map;
}

// ---- meshes ----
GOAT3DAPI int goat3d_add_mesh(struct goat3d *g, struct goat3d_mesh *mesh)
{
	struct goat3d_mesh **arr;
	if(!(arr = dynarr_push(g->meshes, &mesh))) {
		return -1;
	}
	g->meshes = arr;
	return 0;
}

GOAT3DAPI int goat3d_get_mesh_count(struct goat3d *g)
{
	return dynarr_size(g->meshes);
}

GOAT3DAPI struct goat3d_mesh *goat3d_get_mesh(struct goat3d *g, int idx)
{
	return g->meshes[idx];
}

GOAT3DAPI struct goat3d_mesh *goat3d_get_mesh_by_name(struct goat3d *g, const char *name)
{
	int i, num = dynarr_size(g->meshes);
	for(i=0; i<num; i++) {
		if(strcmp(g->meshes[i]->name, name) == 0) {
			return g->meshes[i];
		}
	}
	return 0;
}

GOAT3DAPI struct goat3d_mesh *goat3d_create_mesh(void)
{
	struct goat3d_mesh *m;

	if(!(m = malloc(sizeof *m))) {
		return 0;
	}
	if(g3dimpl_obj_init((struct object*)m, OBJTYPE_MESH) == -1) {
		free(m);
		return 0;
	}
	return m;
}

GOAT3DAPI void goat3d_destroy_mesh(struct goat3d_mesh *mesh)
{
	g3dimpl_obj_destroy((struct object*)m);
}

GOAT3DAPI int goat3d_set_mesh_name(struct goat3d_mesh *mesh, const char *name)
{
	char *tmpname;
	int len = strlen(name);

	if(!(tmpname = malloc(len + 1))) {
		return -1;
	}
	memcpy(tmpname, name, len + 1);
	free(mesh->name);
	mesh->name = tmpname;
	return 0;
}

GOAT3DAPI const char *goat3d_get_mesh_name(const struct goat3d_mesh *mesh)
{
	return mesh->name;
}

GOAT3DAPI void goat3d_set_mesh_mtl(struct goat3d_mesh *mesh, struct goat3d_material *mtl)
{
	mesh->material = mtl;
}

GOAT3DAPI struct goat3d_material *goat3d_get_mesh_mtl(struct goat3d_mesh *mesh)
{
	return mesh->material;
}

GOAT3DAPI int goat3d_get_mesh_attrib_count(struct goat3d_mesh *mesh, enum goat3d_mesh_attrib attrib)
{
	return dynarr_size(mesh->vertices);
}

GOAT3DAPI int goat3d_get_mesh_face_count(struct goat3d_mesh *mesh)
{
	return dynarr_size(mesh->faces);
}

#define SET_VERTEX_DATA(arr, p, n) \
	do { \
		void *tmp = dynarr_resize(arr, n); \
		if(!tmp) { \
			logmsg(LOG_ERROR, "failed to resize vertex array (%d)\n", n); \
			return -1; \
		} \
		arr = tmp; \
		memcpy(arr, p, n * sizeof *arr); \
	} while(0)

GOAT3DAPI int goat3d_set_mesh_attribs(struct goat3d_mesh *mesh, enum goat3d_mesh_attrib attrib, const void *data, int vnum)
{
	if(attrib == GOAT3D_MESH_ATTR_VERTEX) {
		SET_VERTEX_DATA(mesh->vertices, data, vnum);
		return 0;
	}

	if(vnum != dynarr_size(mesh->vertices)) {
		logmsg(LOG_ERROR, "trying to set mesh attrib data with number of elements different than the vertex array\n");
		return -1;
	}

	switch(attrib) {
	case GOAT3D_MESH_ATTR_NORMAL:
		SET_VERTEX_DATA(mesh->normal, data, vnum);
		break;
	case GOAT3D_MESH_ATTR_TANGENT:
		SET_VERTEX_DATA(mesh->tangents, data, vnum);
		break;
	case GOAT3D_MESH_ATTR_TEXCOORD:
		SET_VERTEX_DATA(mesh->texcoords, data, vnum);
		break;
	case GOAT3D_MESH_ATTR_SKIN_WEIGHT:
		SET_VERTEX_DATA(mesh->skin_weights, data, vnum);
		break;
	case GOAT3D_MESH_ATTR_SKIN_MATRIX:
		SET_VERTEX_DATA(mesh->skin_matrices, data, vnum);
		break;
	case GOAT3D_MESH_ATTR_COLOR:
		SET_VERTEX_DATA(mesh->colors, data, vnum);
	default:
		logmsg(LOG_ERROR, "trying to set unknown vertex attrib: %d\n", attrib);
		return -1;
	}
	return 0;
}

GOAT3DAPI int goat3d_add_mesh_attrib1f(struct goat3d_mesh *mesh, enum goat3d_mesh_attrib attrib,
		float val)
{
	return goat3d_add_mesh_attrib4f(mesh, attrib, val, 0, 0, 1);
}

GOAT3DAPI int goat3d_add_mesh_attrib2f(struct goat3d_mesh *mesh, enum goat3d_mesh_attrib attrib,
		float x, float y)
{
	return goat3d_add_mesh_attrib4f(mesh, attrib, x, y, 0, 1);
}

GOAT3DAPI int goat3d_add_mesh_attrib3f(struct goat3d_mesh *mesh, enum goat3d_mesh_attrib attrib,
		float x, float y, float z)
{
	return goat3d_add_mesh_attrib4f(mesh, attrib, x, y, z, 1);
}

GOAT3DAPI int goat3d_add_mesh_attrib4f(struct goat3d_mesh *mesh, enum goat3d_mesh_attrib attrib,
		float x, float y, float z, float w)
{
	cgm_vec4 vec;
	int4 i4;
	void *tmp;

	switch(attrib) {
	case GOAT3D_MESH_ATTR_VERTEX:
		cgm_vcons(&vec, x, y, z);
		if(!(tmp = dynarr_push(mesh->vertices, &vec))) {
			goto err;
		}
		mesh->vertices = tmp;
		break;

	case GOAT3D_MESH_ATTR_NORMAL:
		cgm_vcons(&vec, x, y, z);
		if(!(tmp = dynarr_push(mesh->normals, &vec))) {
			goto err;
		}
		mesh->normals = tmp;
		break;

	case GOAT3D_MESH_ATTR_TANGENT:
		cgm_vcons(&vec, x, y, z);
		if(!(tmp = dynarr_push(mesh->tangents, &vec))) {
			goto err;
		}
		mesh->tangents = tmp;
		break;

	case GOAT3D_MESH_ATTR_TEXCOORD:
		cgm_vcons(&vec, x, y, 0);
		if(!(tmp = dynarr_push(mesh->texcoords, &vec))) {
			goto err;
		}
		mesh->texcoords = tmp;
		break;

	case GOAT3D_MESH_ATTR_SKIN_WEIGHT:
		cgm_wcons(&vec, x, y, z, w);
		if(!(tmp = dynarr_push(mesh->skin_weights, &vec))) {
			goto err;
		}
		mesh->skin_weights = tmp;
		break;

	case GOAT3D_MESH_ATTR_SKIN_MATRIX:
		intvec.x = x;
		intvec.y = y;
		intvec.z = z;
		intvec.w = w;
		if(!(tmp = dynarr_push(mesh->skin_matrices, &intvec))) {
			goto err;
		}
		mesh->skin_matrices = tmp;
		break;

	case GOAT3D_MESH_ATTR_COLOR:
		cgm_wcons(&vec, x, y, z, w);
		if(!(tmp = dynarr_push(mesh->colors, &vec))) {
			goto err;
		}
		mesh->colors = tmp;

	default:
		logmsg(LOG_ERROR, "trying to add unknown vertex attrib: %d\n", attrib);
		return -1;
	}
	return 0;

err:
	logmsg(LOG_ERROR, "failed to push vertex attrib\n");
	return -1;
}

GOAT3DAPI void *goat3d_get_mesh_attribs(struct goat3d_mesh *mesh, enum goat3d_mesh_attrib attrib)
{
	return goat3d_get_mesh_attrib(mesh, attrib, 0);
}

GOAT3DAPI void *goat3d_get_mesh_attrib(struct goat3d_mesh *mesh, enum goat3d_mesh_attrib attrib, int idx)
{
	switch(attrib) {
	case GOAT3D_MESH_ATTR_VERTEX:
		return dynarr_empty(mesh->vertices) ? 0 : mesh->vertices + idx;
	case GOAT3D_MESH_ATTR_NORMAL:
		return dynarr_empty(mesh->normals) ? 0 : mesh->normals + idx;
	case GOAT3D_MESH_ATTR_TANGENT:
		return dynarr_empty(mesh->tangents) ? 0 : mesh->tangents + idx;
	case GOAT3D_MESH_ATTR_TEXCOORD:
		return dynarr_empty(mesh->texcoords) ? 0 : mesh->texcoords + idx;
	case GOAT3D_MESH_ATTR_SKIN_WEIGHT:
		return dynarr_empty(mesh->skin_weights) ? 0 : mesh->skin_weights + idx;
	case GOAT3D_MESH_ATTR_SKIN_MATRIX:
		return dynarr_empty(mesh->skin_matrices) ? 0 : mesh->skin_matrices + idx;
	case GOAT3D_MESH_ATTR_COLOR:
		return dynarr_empty(mesh->colors) ? 0 : mesh->colors + idx;
	default:
		break;
	}
	return 0;
}


GOAT3DAPI int goat3d_set_mesh_faces(struct goat3d_mesh *mesh, const int *data, int num)
{
	void *tmp;
	if(!(tmp = dynarr_resize(mesh->faces, num))) {
		logmsg(LOG_ERROR, "failed to resize face array (%d)\n", num);
		return -1;
	}
	mesh->faces = tmp;
	memcpy(mesh->faces, data, num * sizeof *mesh->faces);
	return 0;
}

GOAT3DAPI int goat3d_add_mesh_face(struct goat3d_mesh *mesh, int a, int b, int c)
{
	void *tmp;
	struct face face;

	face.v[0] = a;
	face.v[1] = b;
	face.v[2] = c;

	if(!(tmp = dynarr_push(mesh->faces, &face))) {
		logmsg(LOG_ERROR, "failed to add face\n");
		return -1;
	}
	mesh->faces = tmp;
	return 0;
}

GOAT3DAPI int *goat3d_get_mesh_faces(struct goat3d_mesh *mesh)
{
	return goat3d_get_mesh_face(mesh, 0);
}

GOAT3DAPI int *goat3d_get_mesh_face(struct goat3d_mesh *mesh, int idx)
{
	return dynarr_empty(mesh->faces) ? 0 : &mesh->faces[idx].v;
}

// immedate mode state
static enum goat3d_im_primitive im_prim;
static struct goat3d_mesh *im_mesh;
static cgm_vec3 im_norm, im_tang;
static cgm_vec2 im_texcoord;
static cgm_vec4 im_skinw, im_color = {1, 1, 1, 1};
static int4 im_skinmat;
static int im_use[NUM_GOAT3D_MESH_ATTRIBS];


GOAT3DAPI void goat3d_begin(struct goat3d_mesh *mesh, enum goat3d_im_primitive prim)
{
	dynarr_clear(mesh->vertices);
	dynarr_clear(mesh->normals);
	dynarr_clear(mesh->tangents);
	dynarr_clear(mesh->texcoords);
	dynarr_clear(mesh->skin_weights);
	dynarr_clear(mesh->skin_matrices);
	dynarr_clear(mesh->colors);
	dynarr_clear(mesh->faces);

	im_mesh = mesh;
	memset(im_use, 0, sizeof im_use);

	im_prim = prim;
}

GOAT3DAPI void goat3d_end(void)
{
	int i, vidx, num_faces, num_quads;
	void *tmp;

	switch(im_prim) {
	case GOAT3D_TRIANGLES:
		{
			num_faces = dynarr_size(im_mesh->vertices) / 3;
			if(!(tmp = dynarr_resize(im_mesh->faces, num_faces))) {
				return;
			}
			im_mesh->faces = tmp;

			vidx = 0;
			for(i=0; i<num_faces; i++) {
				im_mesh->faces[i].v[0] = vidx++;
				im_mesh->faces[i].v[1] = vidx++;
				im_mesh->faces[i].v[2] = vidx++;
			}
		}
		break;

	case GOAT3D_QUADS:
		{
			num_quads = dynarr_size(im_mesh->vertices) / 4;
			if(!(tmp = dynarr_resize(im_mesh->faces, num_quads * 2))) {
				return;
			}
			im_mesh->faces = tmp;

			vidx = 0;
			for(i=0; i<num_quads; i++) {
				im_mesh->faces[i * 2].v[0] = vidx;
				im_mesh->faces[i * 2].v[1] = vidx + 1;
				im_mesh->faces[i * 2].v[2] = vidx + 2;

				im_mesh->faces[i * 2 + 1].v[0] = vidx;
				im_mesh->faces[i * 2 + 1].v[1] = vidx + 2;
				im_mesh->faces[i * 2 + 1].v[2] = vidx + 3;

				vidx += 4;
			}
		}
		break;

	default:
		break;
	}
}

GOAT3DAPI void goat3d_vertex3f(float x, float y, float z)
{
	void *tmp;
	cgm_vec3 v;

	cgm_vcons(&v, x, y, z);
	if(!(tmp = dynarr_push(im_mesh->vertices, &vec))) {
		return;
	}
	im_mesh->vertices = tmp;

	if(im_use[GOAT3D_MESH_ATTR_NORMAL]) {
		if((tmp = dynarr_push(im_mesh->normals, &im_norm))) {
			im_mesh->normals = tmp;
		}
	}
	if(im_use[GOAT3D_MESH_ATTR_TANGENT]) {
		if((tmp = dynarr_push(im_mesh->tangents, &im_tang))) {
			im_mesh->tangents = tmp;
		}
	}
	if(im_use[GOAT3D_MESH_ATTR_TEXCOORD]) {
		if((tmp = dynarr_push(im_mesh->texcoords, &im_texcoord))) {
			im_mesh->texcoords = tmp;
		}
	}
	if(im_use[GOAT3D_MESH_ATTR_SKIN_WEIGHT]) {
		if((tmp = dynarr_push(im_mesh->skin_weights, &im_skinw))) {
			im_mesh->skin_weights = tmp;
		}
	}
	if(im_use[GOAT3D_MESH_ATTR_SKIN_MATRIX]) {
		if((tmp = dynarr_push(im_mesh->skin_matrices, &im_skinmat))) {
			im_mesh->skin_matrices = tmp;
		}
	}
	if(im_use[GOAT3D_MESH_ATTR_COLOR]) {
		if((tmp = dynarr_push(im_mesh->colors, &im_color))) {
			im_mesh->colors = tmp;
		}
	}
}

GOAT3DAPI void goat3d_normal3f(float x, float y, float z)
{
	cgm_vcons(&im_norm, x, y, z);
	im_use[GOAT3D_MESH_ATTR_NORMAL] = 1;
}

GOAT3DAPI void goat3d_tangent3f(float x, float y, float z)
{
	cgm_vcons(&im_tang, x, y, z);
	im_use[GOAT3D_MESH_ATTR_TANGENT] = 1;
}

GOAT3DAPI void goat3d_texcoord2f(float x, float y)
{
	im_texcoord.x = x;
	im_texcoord.y = y;
	im_use[GOAT3D_MESH_ATTR_TEXCOORD] = 1;
}

GOAT3DAPI void goat3d_skin_weight4f(float x, float y, float z, float w)
{
	cgm_wcons(&im_skinw, x, y, z, w);
	im_use[GOAT3D_MESH_ATTR_SKIN_WEIGHT] = 1;
}

GOAT3DAPI void goat3d_skin_matrix4i(int x, int y, int z, int w)
{
	im_skinmat.x = x;
	im_skinmat.y = y;
	im_skinmat.z = z;
	im_skinmat.w = w;
	im_use[GOAT3D_MESH_ATTR_SKIN_MATRIX] = 1;
}

GOAT3DAPI void goat3d_color3f(float x, float y, float z)
{
	goat3d_color4f(x, y, z, 1.0f);
}

GOAT3DAPI void goat3d_color4f(float x, float y, float z, float w)
{
	cgm_wcons(&im_color, x, y, z, w);
	im_use[GOAT3D_MESH_ATTR_COLOR] = 1;
}

// TODO cont.

GOAT3DAPI void goat3d_get_mesh_bounds(const struct goat3d_mesh *mesh, float *bmin, float *bmax)
{
	AABox box = mesh->get_bounds(Matrix4x4::identity);

	for(int i=0; i<3; i++) {
		bmin[i] = box.bmin[i];
		bmax[i] = box.bmax[i];
	}
}

/* lights */
GOAT3DAPI void goat3d_add_light(struct goat3d *g, struct goat3d_light *lt)
{
	g->scn->add_light(lt);
}

GOAT3DAPI int goat3d_get_light_count(struct goat3d *g)
{
	return g->scn->get_light_count();
}

GOAT3DAPI struct goat3d_light *goat3d_get_light(struct goat3d *g, int idx)
{
	return (goat3d_light*)g->scn->get_light(idx);
}

GOAT3DAPI struct goat3d_light *goat3d_get_light_by_name(struct goat3d *g, const char *name)
{
	return (goat3d_light*)g->scn->get_light(name);
}


GOAT3DAPI struct goat3d_light *goat3d_create_light(void)
{
	return new goat3d_light;
}

GOAT3DAPI void goat3d_destroy_light(struct goat3d_light *lt)
{
	delete lt;
}


/* cameras */
GOAT3DAPI void goat3d_add_camera(struct goat3d *g, struct goat3d_camera *cam)
{
	g->scn->add_camera(cam);
}

GOAT3DAPI int goat3d_get_camera_count(struct goat3d *g)
{
	return g->scn->get_camera_count();
}

GOAT3DAPI struct goat3d_camera *goat3d_get_camera(struct goat3d *g, int idx)
{
	return (goat3d_camera*)g->scn->get_camera(idx);
}

GOAT3DAPI struct goat3d_camera *goat3d_get_camera_by_name(struct goat3d *g, const char *name)
{
	return (goat3d_camera*)g->scn->get_camera(name);
}

GOAT3DAPI struct goat3d_camera *goat3d_create_camera(void)
{
	return new goat3d_camera;
}

GOAT3DAPI void goat3d_destroy_camera(struct goat3d_camera *cam)
{
	delete cam;
}



// node
GOAT3DAPI void goat3d_add_node(struct goat3d *g, struct goat3d_node *node)
{
	g->scn->add_node(node);
}

GOAT3DAPI int goat3d_get_node_count(struct goat3d *g)
{
	return g->scn->get_node_count();
}

GOAT3DAPI struct goat3d_node *goat3d_get_node(struct goat3d *g, int idx)
{
	return (goat3d_node*)g->scn->get_node(idx);
}

GOAT3DAPI struct goat3d_node *goat3d_get_node_by_name(struct goat3d *g, const char *name)
{
	return (goat3d_node*)g->scn->get_node(name);
}

GOAT3DAPI struct goat3d_node *goat3d_create_node(void)
{
	return new goat3d_node;
}

GOAT3DAPI void goat3d_set_node_name(struct goat3d_node *node, const char *name)
{
	node->set_name(name);
}

GOAT3DAPI const char *goat3d_get_node_name(const struct goat3d_node *node)
{
	return node->get_name();
}

GOAT3DAPI void goat3d_set_node_object(struct goat3d_node *node, enum goat3d_node_type type, void *obj)
{
	node->set_object((Object*)obj);
}

GOAT3DAPI void *goat3d_get_node_object(const struct goat3d_node *node)
{
	return (void*)node->get_object();
}

GOAT3DAPI enum goat3d_node_type goat3d_get_node_type(const struct goat3d_node *node)
{
	const Object *obj = node->get_object();
	if(dynamic_cast<const Mesh*>(obj)) {
		return GOAT3D_NODE_MESH;
	}
	if(dynamic_cast<const Light*>(obj)) {
		return GOAT3D_NODE_LIGHT;
	}
	if(dynamic_cast<const Camera*>(obj)) {
		return GOAT3D_NODE_CAMERA;
	}

	return GOAT3D_NODE_NULL;
}

GOAT3DAPI void goat3d_add_node_child(struct goat3d_node *node, struct goat3d_node *child)
{
	node->add_child(child);
}

GOAT3DAPI int goat3d_get_node_child_count(const struct goat3d_node *node)
{
	return node->get_children_count();
}

GOAT3DAPI struct goat3d_node *goat3d_get_node_child(const struct goat3d_node *node, int idx)
{
	return (goat3d_node*)node->get_child(idx);
}

GOAT3DAPI struct goat3d_node *goat3d_get_node_parent(const struct goat3d_node *node)
{
	return (goat3d_node*)node->get_parent();
}

GOAT3DAPI void goat3d_use_anim(struct goat3d_node *node, int idx)
{
	node->use_animation(idx);
}

GOAT3DAPI void goat3d_use_anims(struct goat3d_node *node, int aidx, int bidx, float t)
{
	node->use_animation(aidx, bidx, t);
}

GOAT3DAPI void goat3d_use_anim_by_name(struct goat3d_node *node, const char *name)
{
	node->use_animation(name);
}

GOAT3DAPI void goat3d_use_anims_by_name(struct goat3d_node *node, const char *aname, const char *bname, float t)
{
	node->use_animation(aname, bname, t);
}

GOAT3DAPI int goat3d_get_active_anim(struct goat3d_node *node, int which)
{
	return node->get_active_animation_index(which);
}

GOAT3DAPI float goat3d_get_active_anim_mix(struct goat3d_node *node)
{
	return node->get_active_animation_mix();
}

GOAT3DAPI int goat3d_get_anim_count(struct goat3d_node *node)
{
	return node->get_animation_count();
}

GOAT3DAPI void goat3d_add_anim(struct goat3d_node *root)
{
	root->add_animation();
}

GOAT3DAPI void goat3d_set_anim_name(struct goat3d_node *root, const char *name)
{
	root->set_animation_name(name);
}

GOAT3DAPI const char *goat3d_get_anim_name(struct goat3d_node *node)
{
	return node->get_animation_name();
}

GOAT3DAPI long goat3d_get_anim_timeline(struct goat3d_node *root, long *tstart, long *tend)
{
	if(root->get_timeline_bounds(tstart, tend)) {
		return *tend - *tstart;
	}
	return -1;
}

GOAT3DAPI int goat3d_get_node_position_key_count(struct goat3d_node *node)
{
	return node->get_position_key_count();
}

GOAT3DAPI int goat3d_get_node_rotation_key_count(struct goat3d_node *node)
{
	return node->get_rotation_key_count();
}

GOAT3DAPI int goat3d_get_node_scaling_key_count(struct goat3d_node *node)
{
	return node->get_scaling_key_count();
}

GOAT3DAPI long goat3d_get_node_position_key(struct goat3d_node *node, int idx, float *xptr, float *yptr, float *zptr)
{
	Vector3 pos = node->get_position_key_value(idx);
	long tm = node->get_position_key_time(idx);

	if(xptr) *xptr = pos.x;
	if(yptr) *yptr = pos.y;
	if(zptr) *zptr = pos.z;
	return tm;
}

GOAT3DAPI long goat3d_get_node_rotation_key(struct goat3d_node *node, int idx, float *xptr, float *yptr, float *zptr, float *wptr)
{
	Quaternion rot = node->get_rotation_key_value(idx);
	long tm = node->get_rotation_key_time(idx);

	if(xptr) *xptr = rot.v.x;
	if(yptr) *yptr = rot.v.y;
	if(zptr) *zptr = rot.v.z;
	if(wptr) *wptr = rot.s;
	return tm;
}

GOAT3DAPI long goat3d_get_node_scaling_key(struct goat3d_node *node, int idx, float *xptr, float *yptr, float *zptr)
{
	Vector3 scale = node->get_scaling_key_value(idx);
	long tm = node->get_scaling_key_time(idx);

	if(xptr) *xptr = scale.x;
	if(yptr) *yptr = scale.y;
	if(zptr) *zptr = scale.z;
	return tm;
}

GOAT3DAPI void goat3d_set_node_position(struct goat3d_node *node, float x, float y, float z, long tmsec)
{
	node->set_position(Vector3(x, y, z), tmsec);
}

GOAT3DAPI void goat3d_set_node_rotation(struct goat3d_node *node, float qx, float qy, float qz, float qw, long tmsec)
{
	node->set_rotation(Quaternion(qw, qx, qy, qz), tmsec);
}

GOAT3DAPI void goat3d_set_node_scaling(struct goat3d_node *node, float sx, float sy, float sz, long tmsec)
{
	node->set_scaling(Vector3(sx, sy, sz), tmsec);
}

GOAT3DAPI void goat3d_set_node_pivot(struct goat3d_node *node, float px, float py, float pz)
{
	node->set_pivot(Vector3(px, py, pz));
}


GOAT3DAPI void goat3d_get_node_position(const struct goat3d_node *node, float *xptr, float *yptr, float *zptr, long tmsec)
{
	Vector3 pos = node->get_node_position(tmsec);
	*xptr = pos.x;
	*yptr = pos.y;
	*zptr = pos.z;
}

GOAT3DAPI void goat3d_get_node_rotation(const struct goat3d_node *node, float *xptr, float *yptr, float *zptr, float *wptr, long tmsec)
{
	Quaternion q = node->get_node_rotation(tmsec);
	*xptr = q.v.x;
	*yptr = q.v.y;
	*zptr = q.v.z;
	*wptr = q.s;
}

GOAT3DAPI void goat3d_get_node_scaling(const struct goat3d_node *node, float *xptr, float *yptr, float *zptr, long tmsec)
{
	Vector3 scale = node->get_node_scaling(tmsec);
	*xptr = scale.x;
	*yptr = scale.y;
	*zptr = scale.z;
}

GOAT3DAPI void goat3d_get_node_pivot(const struct goat3d_node *node, float *xptr, float *yptr, float *zptr)
{
	Vector3 pivot = node->get_pivot();
	*xptr = pivot.x;
	*yptr = pivot.y;
	*zptr = pivot.z;
}


GOAT3DAPI void goat3d_get_node_matrix(const struct goat3d_node *node, float *matrix, long tmsec)
{
	node->get_node_xform(tmsec, (Matrix4x4*)matrix);
}

GOAT3DAPI void goat3d_get_node_bounds(const struct goat3d_node *node, float *bmin, float *bmax)
{
	AABox box = node->get_bounds();

	for(int i=0; i<3; i++) {
		bmin[i] = box.bmin[i];
		bmax[i] = box.bmax[i];
	}
}


static long read_file(void *buf, size_t bytes, void *uptr)
{
	return (long)fread(buf, 1, bytes, (FILE*)uptr);
}

static long write_file(const void *buf, size_t bytes, void *uptr)
{
	return (long)fwrite(buf, 1, bytes, (FILE*)uptr);
}

static long seek_file(long offs, int whence, void *uptr)
{
	if(fseek((FILE*)uptr, offs, whence) == -1) {
		return -1;
	}
	return ftell((FILE*)uptr);
}

std::string g3dimpl::clean_filename(const char *str)
{
	const char *last_slash = strrchr(str, '/');
	if(!last_slash) {
		last_slash = strrchr(str, '\\');
	}

	if(last_slash) {
		str = last_slash + 1;
	}

	char *buf = (char*)alloca(strlen(str) + 1);
	char *dest = buf;
	while(*str) {
		char c = *str++;
		*dest++ = tolower(c);
	}
	*dest = 0;
	return buf;
}
