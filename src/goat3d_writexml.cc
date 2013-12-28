#include <list>
#include <stdarg.h>
#include "goat3d_impl.h"
#include "anim/anim.h"
#include "log.h"

using namespace g3dimpl;

static bool write_material(const Scene *scn, goat3d_io *io, const Material *mat, int level);
static bool write_mesh(const Scene *scn, goat3d_io *io, const Mesh *mesh, int idx, int level);
static bool write_light(const Scene *scn, goat3d_io *io, const Light *light, int level);
static bool write_camera(const Scene *scn, goat3d_io *io, const Camera *cam, int level);
static bool write_node(const Scene *scn, goat3d_io *io, const Node *node, int level);
static bool write_node_anim(goat3d_io *io, const XFormNode *node, int level);
static void xmlout(goat3d_io *io, int level, const char *fmt, ...);

bool Scene::savexml(goat3d_io *io) const
{
	xmlout(io, 0, "<scene>\n");

	// write environment stuff
	xmlout(io, 1, "<env>\n");
	xmlout(io, 2, "<ambient float3=\"%g %g %g\"/>\n", ambient.x, ambient.y, ambient.z);
	xmlout(io, 1, "</env>\n\n");

	for(size_t i=0; i<materials.size(); i++) {
		write_material(this, io, materials[i], 1);
	}
	for(size_t i=0; i<meshes.size(); i++) {
		write_mesh(this, io, meshes[i], i, 1);
	}
	for(size_t i=0; i<lights.size(); i++) {
		write_light(this, io, lights[i], 1);
	}
	for(size_t i=0; i<cameras.size(); i++) {
		write_camera(this, io, cameras[i], 1);
	}
	for(size_t i=0; i<nodes.size(); i++) {
		write_node(this, io, nodes[i], 1);
	}

	xmlout(io, 0, "</scene>\n");
	return true;
}

static void collect_nodes(std::list<const XFormNode*> *res, const XFormNode *node)
{
	if(!node) return;

	res->push_back(node);

	for(int i=0; i<node->get_children_count(); i++) {
		collect_nodes(res, node->get_child(i));
	}
}

bool Scene::save_anim_xml(const XFormNode *node, goat3d_io *io) const
{
	xmlout(io, 0, "<anim>\n");

	const char *anim_name = node->get_animation_name();
	if(anim_name && *anim_name) {
		xmlout(io, 1, "<name string=\"%s\"/>\n", anim_name);
	}

	std::list<const XFormNode*> allnodes;
	collect_nodes(&allnodes, node);

	std::list<const XFormNode*>::const_iterator it = allnodes.begin();
	while(it != allnodes.end()) {
		const XFormNode *n = *it++;
		write_node_anim(io, n, 1);
	}

	xmlout(io, 0, "</anim>\n");
	return true;
}

static bool write_material(const Scene *scn, goat3d_io *io, const Material *mat, int level)
{
	xmlout(io, level, "<mtl>\n");
	xmlout(io, level + 1, "<name string=\"%s\"/>\n", mat->name.c_str());

	for(int i=0; i<mat->get_attrib_count(); i++) {
		xmlout(io, level + 1, "<attr>\n");
		xmlout(io, level + 2, "<name string=\"%s\"/>\n", mat->get_attrib_name(i));

		const MaterialAttrib &attr = (*mat)[i];
		xmlout(io, level + 2, "<val float4=\"%g %g %g %g\"/>\n", attr.value.x,
				attr.value.y, attr.value.z, attr.value.w);
		if(!attr.map.empty()) {
			xmlout(io, level + 2, "<map string=\"%s\"/>\n", attr.map.c_str());
		}
		xmlout(io, level + 1, "</attr>\n");
	}
	xmlout(io, level, "</mtl>\n\n");
	return true;
}

static bool write_mesh(const Scene *scn, goat3d_io *io, const Mesh *mesh, int idx, int level)
{
	// first write the external (openctm) mesh file
	const char *prefix = scn->get_name();
	if(!prefix) {
		prefix = "goat";
	}

	char *mesh_filename = (char*)alloca(strlen(prefix) + 32);
	sprintf(mesh_filename, "%s-mesh%04d.ctm", prefix, idx);

	if(!mesh->save(mesh_filename)) {
		return false;
	}

	// then refer to that filename in the XML tags
	xmlout(io, level, "<mesh>\n");
	xmlout(io, level + 1, "<name string=\"%s\"/>\n", mesh->name.c_str());
	if(mesh->material) {
		xmlout(io, level + 1, "<material string=\"%s\"/>\n", mesh->material->name.c_str());
	}
	xmlout(io, level + 1, "<file string=\"%s\"/>\n", goat3d_clean_filename(mesh_filename).c_str());
	xmlout(io, level, "</mesh>\n\n");
	return true;
}

static bool write_light(const Scene *scn, goat3d_io *io, const Light *light, int level)
{
	return true;
}

static bool write_camera(const Scene *scn, goat3d_io *io, const Camera *cam, int level)
{
	return true;
}

static bool write_node(const Scene *scn, goat3d_io *io, const Node *node, int level)
{
	xmlout(io, level, "<node>\n");
	xmlout(io, level + 1, "<name string=\"%s\"/>\n", node->get_name());

	const XFormNode *parent = node->get_parent();
	if(parent) {
		xmlout(io, level + 1, "<parent string=\"%s\"/>\n", parent->get_name());
	}

	const char *type = 0;
	const Object *obj = node->get_object();
	if(dynamic_cast<const Mesh*>(obj)) {
		type = "mesh";
	} else if(dynamic_cast<const Light*>(obj)) {
		type = "light";
	} else if(dynamic_cast<const Camera*>(obj)) {
		type = "camera";
	}

	if(type) {
		xmlout(io, level + 1, "<%s string=\"%s\"/>\n", type, obj->name.c_str());
	}

	Vector3 pos = node->get_node_position();
	Quaternion rot = node->get_node_rotation();
	Vector3 scale = node->get_node_scaling();
	Vector3 pivot = node->get_pivot();

	Matrix4x4 xform;
	node->get_node_xform(0, &xform);

	xmlout(io, level + 1, "<pos float3=\"%g %g %g\"/>\n", pos.x, pos.y, pos.z);
	xmlout(io, level + 1, "<rot float4=\"%g %g %g %g\"/>\n", rot.v.x, rot.v.y, rot.v.z, rot.s);
	xmlout(io, level + 1, "<scale float3=\"%g %g %g\"/>\n", scale.x, scale.y, scale.z);
	xmlout(io, level + 1, "<pivot float3=\"%g %g %g\"/>\n", pivot.x, pivot.y, pivot.z);

	xmlout(io, level + 1, "<matrix0 float4=\"%g %g %g %g\"/>\n", xform[0][0], xform[0][1], xform[0][2], xform[0][3]);
	xmlout(io, level + 1, "<matrix1 float4=\"%g %g %g %g\"/>\n", xform[1][0], xform[1][1], xform[1][2], xform[1][3]);
	xmlout(io, level + 1, "<matrix2 float4=\"%g %g %g %g\"/>\n", xform[2][0], xform[2][1], xform[2][2], xform[2][3]);

	xmlout(io, level, "</node>\n");
	return true;
}

static bool write_node_anim(goat3d_io *io, const XFormNode *node, int level)
{
	static const char *attr_names[] = { "position", "rotation", "scaling" };
	struct anm_node *anode = node->get_libanim_node();
	struct anm_animation *anim = anm_get_active_animation(anode, 0);

	if(!anode || !anim) {
		return false;
	}

	struct anm_track *trk[4];

	for(int i=0; i<3; i++) {	// 3 attributes
		switch(i) {
		case 0:	// position
			trk[0] = anim->tracks + ANM_TRACK_POS_X;
			trk[1] = anim->tracks + ANM_TRACK_POS_Y;
			trk[2] = anim->tracks + ANM_TRACK_POS_Z;
			trk[3] = 0;
			break;

		case 1:	// rotation
			trk[0] = anim->tracks + ANM_TRACK_ROT_X;
			trk[1] = anim->tracks + ANM_TRACK_ROT_Y;
			trk[2] = anim->tracks + ANM_TRACK_ROT_Z;
			trk[3] = anim->tracks + ANM_TRACK_ROT_W;
			break;

		case 2:	// scaling
			trk[0] = anim->tracks + ANM_TRACK_SCL_X;
			trk[1] = anim->tracks + ANM_TRACK_SCL_Y;
			trk[2] = anim->tracks + ANM_TRACK_SCL_Z;
			trk[3] = 0;
		}

		if(trk[0]->count <= 0) {
			continue;	// skip tracks without any keyframes
		}

		xmlout(io, level + 1, "<track>\n");
		xmlout(io, level + 2, "<node string=\"%s\"/>\n", node->get_name());
		xmlout(io, level + 2, "<attr string=\"%s\"/>\n", attr_names[i]);

		// TODO cont: move all the keyframe retreival to XFormNode and use that...

		xmlout(io, level + 1, "</track>\n");
	}
	return true;
}

static void xmlout(goat3d_io *io, int level, const char *fmt, ...)
{
	for(int i=0; i<level; i++) {
		io_fprintf(io, "  ");
	}

	va_list ap;
	va_start(ap, fmt);
	io_vfprintf(io, fmt, ap);
	va_end(ap);
}
