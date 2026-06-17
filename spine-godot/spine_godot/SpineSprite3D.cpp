/******************************************************************************
 * Spine Runtimes License Agreement
 * Last updated April 5, 2025. Replaces all prior versions.
 *
 * Copyright (c) 2013-2025, Esoteric Software LLC
 *
 * Integration of the Spine Runtimes into software or otherwise creating
 * derivative works of the Spine Runtimes is permitted under the terms and
 * conditions of Section 2 of the Spine Editor License Agreement:
 * http://esotericsoftware.com/spine-editor-license
 *
 * Otherwise, it is permitted to integrate the Spine Runtimes into software
 * or otherwise create derivative works of the Spine Runtimes (collectively,
 * "Products"), provided that each user of the Products must obtain their own
 * Spine Editor license and redistribution of the Products in any form must
 * include this license and copyright notice.
 *
 * THE SPINE RUNTIMES ARE PROVIDED BY ESOTERIC SOFTWARE LLC "AS IS" AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL ESOTERIC SOFTWARE LLC BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES,
 * BUSINESS INTERRUPTION, OR LOSS OF USE, DATA, OR PROFITS) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THE SPINE RUNTIMES, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *****************************************************************************/

#include "SpineCommon.h"

#if VERSION_MAJOR > 3

#include "SpineSprite3D.h"
#include "SpineSpriteCommon.h"
#include "SpineEvent.h"
#include "SpineTrackEntry.h"
#include "SpineRendererObject.h"
#include "SpineSkin.h"
#include "SpineAtlasResource.h"

#ifdef SPINE_GODOT_EXTENSION
#include <godot_cpp/core/memory.hpp>
#include <godot_cpp/classes/texture2d.hpp>
#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/classes/immediate_mesh.hpp>
#include <godot_cpp/classes/rendering_server.hpp>
#else
#include "core/os/memory.h"
#include "core/config/engine.h"
#include "core/math/triangle_mesh.h"
#include "scene/resources/immediate_mesh.h"
#if (VERSION_MAJOR >= 4 && VERSION_MINOR >= 6)
#include "servers/rendering/rendering_server.h"
#else
#include "servers/rendering_server.h"
#endif
#ifdef TOOLS_ENABLED
#if (VERSION_MAJOR >= 4 && VERSION_MINOR >= 5)
#include "editor/file_system/editor_file_system.h"
#else
#include "editor/editor_file_system.h"
#endif
#endif
#endif

static const float SPINE_SLOT_SORT_Z_STEP = 0.001f;
// Applied along the camera view axis for transparent sort. Unlike local Z node offsets,
// this keeps Spine slot draw order correct when the camera rotates around the sprite.
static const float SPINE_SLOT_SORTING_OFFSET_STEP = 0.001f;

struct SpineSprite3DSavedTrack {
	int track_index = 0;
	String animation_name;
	bool loop = false;
};

static void spine_sprite3d_save_animation_tracks(const Ref<SpineAnimationState> &p_animation_state, Vector<SpineSprite3DSavedTrack> &r_saved_tracks) {
	r_saved_tracks.clear();
	if (!p_animation_state.is_valid() || !p_animation_state->get_spine_object()) {
		return;
	}
	spine::AnimationState *previous_state = p_animation_state->get_spine_object();
	spine::Array<spine::TrackEntry *> &tracks = previous_state->getTracks();
	for (int i = 0; i < (int)tracks.size(); i++) {
		spine::TrackEntry *entry = tracks[i];
		if (!entry) {
			continue;
		}
		SpineSprite3DSavedTrack saved;
		saved.track_index = i;
		saved.animation_name = String(entry->getAnimation().getName().buffer());
		saved.loop = entry->getLoop();
		r_saved_tracks.push_back(saved);
	}
}

static void spine_sprite3d_restore_animation_tracks(const Ref<SpineAnimationState> &p_animation_state, const Vector<SpineSprite3DSavedTrack> &p_saved_tracks) {
	if (!p_animation_state.is_valid() || !p_animation_state->get_spine_object()) {
		return;
	}
	for (int i = 0; i < p_saved_tracks.size(); i++) {
		const SpineSprite3DSavedTrack &saved = p_saved_tracks[i];
		p_animation_state->set_animation(saved.animation_name, saved.loop, saved.track_index);
	}
}

static void update_preview_animation(SpineSprite3D *sprite, const String &skin, const String &animation, bool frame, float time);

struct SpineSprite3DStatics {
private:
	static SpineSprite3DStatics *_instance;

public:
	Ref<StandardMaterial3D> default_materials[4] = {};
	int sprite_count = 0;
	spine::Array<unsigned short> quad_indices;
	spine::Array<float> scratch_vertices;

	SpineSprite3DStatics() {
		quad_indices.setSize(6, 0);
		quad_indices[0] = 0;
		quad_indices[1] = 1;
		quad_indices[2] = 2;
		quad_indices[3] = 2;
		quad_indices[4] = 3;
		quad_indices[5] = 0;
		scratch_vertices.ensureCapacity(1200);

		auto make_material = [](BaseMaterial3D::BlendMode blend, BaseMaterial3D::Transparency transparency) {
			Ref<StandardMaterial3D> material(memnew(StandardMaterial3D));
			material->set_albedo(Color(1, 1, 1, 1));
			material->set_transparency(transparency);
			material->set_blend_mode(blend);
			material->set_cull_mode(BaseMaterial3D::CULL_DISABLED);
			material->set_shading_mode(BaseMaterial3D::SHADING_MODE_UNSHADED);
			material->set_flag(BaseMaterial3D::FLAG_ALBEDO_FROM_VERTEX_COLOR, true);
			material->set_flag(BaseMaterial3D::FLAG_SRGB_VERTEX_COLOR, true);
			material->set_texture_filter(BaseMaterial3D::TEXTURE_FILTER_LINEAR_WITH_MIPMAPS);
			material->set_depth_draw_mode(BaseMaterial3D::DEPTH_DRAW_DISABLED);
			return material;
		};

		default_materials[spine::BlendMode_Normal] = make_material(BaseMaterial3D::BLEND_MODE_MIX, BaseMaterial3D::TRANSPARENCY_ALPHA);
		default_materials[spine::BlendMode_Additive] = make_material(BaseMaterial3D::BLEND_MODE_ADD, BaseMaterial3D::TRANSPARENCY_ALPHA);
		default_materials[spine::BlendMode_Multiply] = make_material(BaseMaterial3D::BLEND_MODE_MUL, BaseMaterial3D::TRANSPARENCY_ALPHA);
		default_materials[spine::BlendMode_Screen] = make_material(BaseMaterial3D::BLEND_MODE_SUB, BaseMaterial3D::TRANSPARENCY_ALPHA);
	}

	void ensure_default_materials() {
		if (default_materials[spine::BlendMode_Normal].is_valid()) {
			return;
		}

		auto make_material = [](BaseMaterial3D::BlendMode blend, BaseMaterial3D::Transparency transparency) {
			Ref<StandardMaterial3D> material(memnew(StandardMaterial3D));
			material->set_albedo(Color(1, 1, 1, 1));
			material->set_transparency(transparency);
			material->set_blend_mode(blend);
			material->set_cull_mode(BaseMaterial3D::CULL_DISABLED);
			material->set_shading_mode(BaseMaterial3D::SHADING_MODE_UNSHADED);
			material->set_flag(BaseMaterial3D::FLAG_ALBEDO_FROM_VERTEX_COLOR, true);
			material->set_flag(BaseMaterial3D::FLAG_SRGB_VERTEX_COLOR, true);
			material->set_texture_filter(BaseMaterial3D::TEXTURE_FILTER_LINEAR_WITH_MIPMAPS);
			material->set_depth_draw_mode(BaseMaterial3D::DEPTH_DRAW_DISABLED);
			return material;
		};

		default_materials[spine::BlendMode_Normal] = make_material(BaseMaterial3D::BLEND_MODE_MIX, BaseMaterial3D::TRANSPARENCY_ALPHA);
		default_materials[spine::BlendMode_Additive] = make_material(BaseMaterial3D::BLEND_MODE_ADD, BaseMaterial3D::TRANSPARENCY_ALPHA);
		default_materials[spine::BlendMode_Multiply] = make_material(BaseMaterial3D::BLEND_MODE_MUL, BaseMaterial3D::TRANSPARENCY_ALPHA);
		default_materials[spine::BlendMode_Screen] = make_material(BaseMaterial3D::BLEND_MODE_SUB, BaseMaterial3D::TRANSPARENCY_ALPHA);
	}

	static SpineSprite3DStatics &instance() {
		if (!_instance) {
			_instance = new SpineSprite3DStatics();
		} else {
			_instance->ensure_default_materials();
		}
		return *_instance;
	}

	static void clear() {
		if (!_instance) {
			return;
		}
		if (_instance->sprite_count > 0) {
			return;
		}
		delete _instance;
		_instance = nullptr;
	}
};

SpineSprite3DStatics *SpineSprite3DStatics::_instance = nullptr;

static bool is_albedo_texture_ready(const Ref<Texture2D> &p_texture) {
	return p_texture.is_valid() && p_texture->get_width() > 0 && p_texture->get_height() > 0;
}

static Ref<Texture2D> texture_ref_from_renderer_object(SpineRendererObject *p_renderer_object) {
	if (!p_renderer_object || !p_renderer_object->texture.is_valid()) {
		return Ref<Texture2D>();
	}
	Ref<Texture2D> albedo_texture = p_renderer_object->texture;
	if (!albedo_texture.is_valid()) {
		albedo_texture = Ref<Texture2D>(Object::cast_to<Texture2D>(p_renderer_object->texture.ptr()));
	}
	return albedo_texture;
}

static SpineRendererObject *renderer_object_from_sequence(spine::Sequence &sequence, spine::SlotPose &pose) {
	const int sequence_index = sequence.resolveIndex(pose);
	spine::TextureRegion *texture_region = sequence.getRegion(sequence_index);
	if (!texture_region || !texture_region->getRTTI().isExactly(spine::AtlasRegion::rtti)) {
		return nullptr;
	}
	spine::AtlasPage *page = ((spine::AtlasRegion *)texture_region)->getPage();
	if (!page) {
		return nullptr;
	}
	return (SpineRendererObject *)page->texture;
}

void SpineMesh3D::_bind_methods() {
}

void SpineMesh3D::set_material(const Ref<Material> &p_material) {
	if (slot_material == p_material) {
		return;
	}
	slot_material = p_material;
	surface_material_dirty = true;
}

SpineMesh3D::SpineMesh3D()
	: renderer_object(nullptr), cached_renderer_object(nullptr), mesh_dirty(true), surface_material_dirty(true), mesh_assigned(false),
	  instance_refresh_needed(false), last_vertex_count(0), last_index_count(0), cached_draw_order(-1) {
#if VERSION_MAJOR > 3
	vertex_stride = 0;
	attribute_stride = 0;
	memset(surface_offsets, 0, sizeof(surface_offsets));
#endif
}

Ref<StandardMaterial3D> SpineMesh3D::build_surface_material(const Ref<Texture2D> &p_albedo_texture, int draw_order) {
	Ref<Material> base_material = slot_material;
	StandardMaterial3D *std_base = Object::cast_to<StandardMaterial3D>(base_material.ptr());
	if (!std_base) {
		std_base = Object::cast_to<StandardMaterial3D>(SpineSprite3DStatics::instance().default_materials[spine::BlendMode_Normal].ptr());
	}
	if (!std_base) {
		return Ref<StandardMaterial3D>();
	}

	Ref<StandardMaterial3D> instance_material = std_base->duplicate();
	instance_material->set_albedo(Color(1, 1, 1, 1));
	instance_material->set_flag(BaseMaterial3D::FLAG_ALBEDO_FROM_VERTEX_COLOR, true);
	instance_material->set_flag(BaseMaterial3D::FLAG_SRGB_VERTEX_COLOR, true);

	SpineSprite3D *sprite = Object::cast_to<SpineSprite3D>(get_parent());
	if (sprite) {
		sprite->configure_slot_material(instance_material.ptr(), draw_order);
	} else {
		instance_material->set_depth_draw_mode(BaseMaterial3D::DEPTH_DRAW_DISABLED);
		instance_material->set_render_priority(draw_order);
	}

	if (p_albedo_texture.is_valid()) {
		instance_material->set_texture(BaseMaterial3D::TEXTURE_ALBEDO, p_albedo_texture);
	}

	return instance_material;
}

void SpineMesh3D::apply_surface_material(bool p_force_instance_refresh) {
	if (!cached_surface_material.is_valid() || !array_mesh.is_valid() || array_mesh->get_surface_count() == 0) {
		set_material_override(Ref<Material>());
		return;
	}
	array_mesh->surface_set_material(0, cached_surface_material);
	set_material_override(cached_surface_material);
	if (p_force_instance_refresh || instance_refresh_needed) {
		Ref<ArrayMesh> mesh_ref = array_mesh;
		set_mesh(Ref<ArrayMesh>());
		set_mesh(mesh_ref);
		instance_refresh_needed = false;
	}
}

void SpineMesh3D::sync_mesh_aabb(const PackedVector3Array &p_vertices) {
	if (!array_mesh.is_valid() || p_vertices.is_empty()) {
		return;
	}
	AABB aabb;
	for (int i = 0; i < p_vertices.size(); i++) {
		if (i == 0) {
			aabb.position = p_vertices[i];
			aabb.size = Vector3();
		} else {
			aabb.expand_to(p_vertices[i]);
		}
	}
	RenderingServer::get_singleton()->mesh_set_custom_aabb(array_mesh->get_rid(), aabb);
}

void SpineMesh3D::clear_mesh_surface() {
	renderer_object = nullptr;
	cached_renderer_object = nullptr;
	if (array_mesh.is_valid() && array_mesh->get_surface_count() > 0) {
		array_mesh->clear_surfaces();
	}
	set_mesh(Ref<ArrayMesh>());
	mesh_assigned = false;
	set_material_override(Ref<Material>());
	pick_vertices.clear();
	pick_indices.clear();
#if VERSION_MAJOR > 3
	vertex_buffer.clear();
	attribute_buffer.clear();
	vertex_stride = 0;
	attribute_stride = 0;
	memset(surface_offsets, 0, sizeof(surface_offsets));
#endif
	cached_surface_material.unref();
	cached_albedo_texture.unref();
	surface_material_dirty = true;
	last_vertex_count = 0;
	last_index_count = 0;
	mesh_dirty = true;
}

void SpineMesh3D::rebuild_mesh_surface(const PackedVector3Array &p_vertices, const PackedVector2Array &p_uvs, const PackedColorArray &p_colors,
									   const PackedInt32Array &p_indices) {
	Array arrays;
	arrays.resize(Mesh::ARRAY_MAX);
	arrays[Mesh::ARRAY_VERTEX] = p_vertices;
	arrays[Mesh::ARRAY_TEX_UV] = p_uvs;
	arrays[Mesh::ARRAY_COLOR] = p_colors;
	arrays[Mesh::ARRAY_INDEX] = p_indices;

	array_mesh->clear_surfaces();
	array_mesh->add_surface_from_arrays(Mesh::PRIMITIVE_TRIANGLES, arrays, Array(), Dictionary(), Mesh::ARRAY_FLAG_USE_DYNAMIC_UPDATE);
#if VERSION_MAJOR > 3
	RenderingServer *rs = RenderingServer::get_singleton();
	RS::SurfaceData surface = rs->mesh_get_surface(array_mesh->get_rid(), 0);
	uint32_t skin_stride = 0;
	uint32_t normal_tangent_stride = 0;
#if VERSION_MINOR > 1
	rs->mesh_surface_make_offsets_from_format(surface.format, surface.vertex_count, surface.index_count, surface_offsets, vertex_stride,
											  normal_tangent_stride, attribute_stride, skin_stride);
#else
	rs->mesh_surface_make_offsets_from_format(surface.format, surface.vertex_count, surface.index_count, surface_offsets, vertex_stride,
											  attribute_stride, skin_stride);
#endif
	vertex_buffer = surface.vertex_data;
	attribute_buffer = surface.attribute_data;
#endif
	sync_mesh_aabb(p_vertices);

	last_vertex_count = p_vertices.size();
	last_index_count = p_indices.size();
	surface_material_dirty = true;
}

void SpineMesh3D::update_mesh_surface_buffers(const PackedVector3Array &p_vertices, const PackedVector2Array &p_uvs,
											  const PackedColorArray &p_colors) {
#if VERSION_MAJOR > 3
	if (vertex_buffer.is_empty() || attribute_buffer.is_empty()) {
		return;
	}

	AABB aabb_new;
	uint8_t *vertex_write_buffer = vertex_buffer.ptrw();
	uint8_t *attribute_write_buffer = attribute_buffer.ptrw();
	for (int i = 0; i < p_vertices.size(); i++) {
		Vector3 vertex = p_vertices[i];
		if (i == 0) {
			aabb_new.position = vertex;
			aabb_new.size = Vector3();
		} else {
			aabb_new.expand_to(vertex);
		}

		const Color &color = p_colors[i];
		uint8_t color_bytes[4] = {uint8_t(CLAMP(color.r * 255.0, 0.0, 255.0)), uint8_t(CLAMP(color.g * 255.0, 0.0, 255.0)),
								  uint8_t(CLAMP(color.b * 255.0, 0.0, 255.0)), uint8_t(CLAMP(color.a * 255.0, 0.0, 255.0))};
		Vector2 uv = p_uvs[i];
		memcpy(&vertex_write_buffer[i * vertex_stride + surface_offsets[RS::ARRAY_VERTEX]], &vertex, sizeof(Vector3));
		memcpy(&attribute_write_buffer[i * attribute_stride + surface_offsets[RS::ARRAY_COLOR]], color_bytes, 4);
		memcpy(&attribute_write_buffer[i * attribute_stride + surface_offsets[RS::ARRAY_TEX_UV]], &uv, sizeof(Vector2));
	}

	RenderingServer *rs = RenderingServer::get_singleton();
	rs->mesh_surface_update_vertex_region(array_mesh->get_rid(), 0, 0, vertex_buffer);
	rs->mesh_surface_update_attribute_region(array_mesh->get_rid(), 0, 0, attribute_buffer);
	rs->mesh_set_custom_aabb(array_mesh->get_rid(), aabb_new);
#endif
	sync_mesh_aabb(p_vertices);
}

void SpineMesh3D::update_mesh(const PackedVector3Array &p_vertices, const PackedVector2Array &p_uvs, const PackedColorArray &p_colors,
							  const PackedInt32Array &p_indices, const Ref<Texture2D> &p_albedo_texture, int draw_order) {
	if (!array_mesh.is_valid()) {
#ifdef SPINE_GODOT_EXTENSION
		array_mesh.instantiate();
#else
		array_mesh = Ref<ArrayMesh>(memnew(ArrayMesh));
#endif
		mesh_assigned = false;
		last_vertex_count = 0;
		last_index_count = 0;
	}

	if (p_vertices.is_empty() || p_indices.is_empty()) {
		clear_mesh_surface();
		return;
	}

	if (!is_albedo_texture_ready(p_albedo_texture)) {
		clear_mesh_surface();
		if (SpineSprite3D *sprite = Object::cast_to<SpineSprite3D>(get_parent())) {
			sprite->schedule_display_refresh();
		}
		return;
	}

	const bool topology_changed = array_mesh->get_surface_count() == 0 || last_vertex_count != p_vertices.size() ||
								  last_index_count != p_indices.size();
	if (topology_changed) {
		rebuild_mesh_surface(p_vertices, p_uvs, p_colors, p_indices);
	} else {
		update_mesh_surface_buffers(p_vertices, p_uvs, p_colors);
	}

	const bool texture_changed = cached_albedo_texture != p_albedo_texture;
	const bool draw_order_changed = cached_draw_order != draw_order;
	if (surface_material_dirty || !cached_surface_material.is_valid() || texture_changed) {
		cached_surface_material = build_surface_material(p_albedo_texture, draw_order);
		cached_albedo_texture = p_albedo_texture;
		cached_draw_order = draw_order;
		surface_material_dirty = false;
		instance_refresh_needed = true;
	} else if (draw_order_changed && cached_surface_material.is_valid()) {
		if (SpineSprite3D *sprite = Object::cast_to<SpineSprite3D>(get_parent())) {
			sprite->configure_slot_material(cached_surface_material.ptr(), draw_order);
		}
		cached_draw_order = draw_order;
	}

	const bool force_instance_refresh = !mesh_assigned || instance_refresh_needed;
	if (!mesh_assigned) {
		set_mesh(array_mesh);
		mesh_assigned = true;
	}
	if (cached_surface_material.is_valid()) {
		apply_surface_material(force_instance_refresh);
	}

	pick_vertices = p_vertices;
	pick_indices = p_indices;
	mesh_dirty = false;
}

void SpineSprite3D::clear_statics() {
	SpineSprite3DStatics::clear();
}

void SpineSprite3D::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_skeleton_data_res", "skeleton_data_res"), &SpineSprite3D::set_skeleton_data_res);
	ClassDB::bind_method(D_METHOD("get_skeleton_data_res"), &SpineSprite3D::get_skeleton_data_res);
	ClassDB::bind_method(D_METHOD("get_skeleton"), &SpineSprite3D::get_skeleton);
	ClassDB::bind_method(D_METHOD("get_animation_state"), &SpineSprite3D::get_animation_state);
	ClassDB::bind_method(D_METHOD("on_skeleton_data_changed"), &SpineSprite3D::on_skeleton_data_changed);
	ClassDB::bind_method(D_METHOD("teardown_spine_objects"), &SpineSprite3D::teardown_spine_objects);
	ClassDB::bind_method(D_METHOD("teardown_mesh_children"), &SpineSprite3D::teardown_mesh_children);
	ClassDB::bind_method(D_METHOD("rebuild_spine_objects"), &SpineSprite3D::rebuild_spine_objects);
	ClassDB::bind_method(D_METHOD("schedule_skeleton_rebuild"), &SpineSprite3D::schedule_skeleton_rebuild);
	ClassDB::bind_method(D_METHOD("visual_settings_changed"), &SpineSprite3D::visual_settings_changed);
	ClassDB::bind_method(D_METHOD("refresh_display"), &SpineSprite3D::refresh_display);
#if defined(TOOLS_ENABLED) && !defined(SPINE_GODOT_EXTENSION)
	ClassDB::bind_method(D_METHOD("on_editor_filesystem_changed"), &SpineSprite3D::on_editor_filesystem_changed);
#endif
	ClassDB::bind_method(D_METHOD("get_global_bone_transform", "bone_name"), &SpineSprite3D::get_global_bone_transform);
	ClassDB::bind_method(D_METHOD("set_global_bone_transform", "bone_name", "global_transform"), &SpineSprite3D::set_global_bone_transform);
	ClassDB::bind_method(D_METHOD("set_update_mode", "v"), &SpineSprite3D::set_update_mode);
	ClassDB::bind_method(D_METHOD("get_update_mode"), &SpineSprite3D::get_update_mode);
	ClassDB::bind_method(D_METHOD("set_normal_material", "material"), &SpineSprite3D::set_normal_material);
	ClassDB::bind_method(D_METHOD("get_normal_material"), &SpineSprite3D::get_normal_material);
	ClassDB::bind_method(D_METHOD("set_additive_material", "material"), &SpineSprite3D::set_additive_material);
	ClassDB::bind_method(D_METHOD("get_additive_material"), &SpineSprite3D::get_additive_material);
	ClassDB::bind_method(D_METHOD("set_multiply_material", "material"), &SpineSprite3D::set_multiply_material);
	ClassDB::bind_method(D_METHOD("get_multiply_material"), &SpineSprite3D::get_multiply_material);
	ClassDB::bind_method(D_METHOD("set_screen_material", "material"), &SpineSprite3D::set_screen_material);
	ClassDB::bind_method(D_METHOD("get_screen_material"), &SpineSprite3D::get_screen_material);
	ClassDB::bind_method(D_METHOD("get_time_scale"), &SpineSprite3D::get_time_scale);
	ClassDB::bind_method(D_METHOD("set_time_scale", "v"), &SpineSprite3D::set_time_scale);

	ClassDB::bind_method(D_METHOD("set_debug_root", "v"), &SpineSprite3D::set_debug_root);
	ClassDB::bind_method(D_METHOD("get_debug_root"), &SpineSprite3D::get_debug_root);
	ClassDB::bind_method(D_METHOD("set_debug_root_color", "v"), &SpineSprite3D::set_debug_root_color);
	ClassDB::bind_method(D_METHOD("get_debug_root_color"), &SpineSprite3D::get_debug_root_color);
	ClassDB::bind_method(D_METHOD("set_debug_bones", "v"), &SpineSprite3D::set_debug_bones);
	ClassDB::bind_method(D_METHOD("get_debug_bones"), &SpineSprite3D::get_debug_bones);
	ClassDB::bind_method(D_METHOD("set_debug_bones_color", "v"), &SpineSprite3D::set_debug_bones_color);
	ClassDB::bind_method(D_METHOD("get_debug_bones_color"), &SpineSprite3D::get_debug_bones_color);
	ClassDB::bind_method(D_METHOD("set_debug_bones_thickness", "v"), &SpineSprite3D::set_debug_bones_thickness);
	ClassDB::bind_method(D_METHOD("get_debug_bones_thickness"), &SpineSprite3D::get_debug_bones_thickness);
	ClassDB::bind_method(D_METHOD("set_debug_regions", "v"), &SpineSprite3D::set_debug_regions);
	ClassDB::bind_method(D_METHOD("get_debug_regions"), &SpineSprite3D::get_debug_regions);
	ClassDB::bind_method(D_METHOD("set_debug_regions_color", "v"), &SpineSprite3D::set_debug_regions_color);
	ClassDB::bind_method(D_METHOD("get_debug_regions_color"), &SpineSprite3D::get_debug_regions_color);
	ClassDB::bind_method(D_METHOD("set_debug_meshes", "v"), &SpineSprite3D::set_debug_meshes);
	ClassDB::bind_method(D_METHOD("get_debug_meshes"), &SpineSprite3D::get_debug_meshes);
	ClassDB::bind_method(D_METHOD("set_debug_meshes_color", "v"), &SpineSprite3D::set_debug_meshes_color);
	ClassDB::bind_method(D_METHOD("get_debug_meshes_color"), &SpineSprite3D::get_debug_meshes_color);
	ClassDB::bind_method(D_METHOD("set_debug_bounding_boxes", "v"), &SpineSprite3D::set_debug_bounding_boxes);
	ClassDB::bind_method(D_METHOD("get_debug_bounding_boxes"), &SpineSprite3D::get_debug_bounding_boxes);
	ClassDB::bind_method(D_METHOD("set_debug_bounding_boxes_color", "v"), &SpineSprite3D::set_debug_bounding_boxes_color);
	ClassDB::bind_method(D_METHOD("get_debug_bounding_boxes_color"), &SpineSprite3D::get_debug_bounding_boxes_color);
	ClassDB::bind_method(D_METHOD("set_debug_paths", "v"), &SpineSprite3D::set_debug_paths);
	ClassDB::bind_method(D_METHOD("get_debug_paths"), &SpineSprite3D::get_debug_paths);
	ClassDB::bind_method(D_METHOD("set_debug_paths_color", "v"), &SpineSprite3D::set_debug_paths_color);
	ClassDB::bind_method(D_METHOD("get_debug_paths_color"), &SpineSprite3D::get_debug_paths_color);
	ClassDB::bind_method(D_METHOD("set_debug_clipping", "v"), &SpineSprite3D::set_debug_clipping);
	ClassDB::bind_method(D_METHOD("get_debug_clipping"), &SpineSprite3D::get_debug_clipping);
	ClassDB::bind_method(D_METHOD("set_debug_clipping_color", "v"), &SpineSprite3D::set_debug_clipping_color);
	ClassDB::bind_method(D_METHOD("get_debug_clipping_color"), &SpineSprite3D::get_debug_clipping_color);

	ClassDB::bind_method(D_METHOD("update_skeleton", "delta"), &SpineSprite3D::update_skeleton);
	ClassDB::bind_method(D_METHOD("new_skin", "name"), &SpineSprite3D::new_skin);

	ClassDB::bind_method(D_METHOD("set_flip_h", "flip_h"), &SpineSprite3D::set_flip_h);
	ClassDB::bind_method(D_METHOD("is_flipped_h"), &SpineSprite3D::is_flipped_h);
	ClassDB::bind_method(D_METHOD("set_flip_v", "flip_v"), &SpineSprite3D::set_flip_v);
	ClassDB::bind_method(D_METHOD("is_flipped_v"), &SpineSprite3D::is_flipped_v);
	ClassDB::bind_method(D_METHOD("set_modulate", "modulate"), &SpineSprite3D::set_modulate);
	ClassDB::bind_method(D_METHOD("get_modulate"), &SpineSprite3D::get_modulate);
	ClassDB::bind_method(D_METHOD("set_pixel_size", "pixel_size"), &SpineSprite3D::set_pixel_size);
	ClassDB::bind_method(D_METHOD("get_pixel_size"), &SpineSprite3D::get_pixel_size);
	ClassDB::bind_method(D_METHOD("set_sprite_render_priority", "priority"), &SpineSprite3D::set_sprite_render_priority);
	ClassDB::bind_method(D_METHOD("get_sprite_render_priority"), &SpineSprite3D::get_sprite_render_priority);
	ClassDB::bind_method(D_METHOD("set_draw_flag", "flag", "enabled"), &SpineSprite3D::set_draw_flag);
	ClassDB::bind_method(D_METHOD("get_draw_flag", "flag"), &SpineSprite3D::get_draw_flag);
	ClassDB::bind_method(D_METHOD("set_billboard_mode", "mode"), &SpineSprite3D::set_billboard_mode);
	ClassDB::bind_method(D_METHOD("get_billboard_mode"), &SpineSprite3D::get_billboard_mode);
	ClassDB::bind_method(D_METHOD("set_texture_filter", "mode"), &SpineSprite3D::set_texture_filter);
	ClassDB::bind_method(D_METHOD("get_texture_filter"), &SpineSprite3D::get_texture_filter);
	ClassDB::bind_method(D_METHOD("generate_triangle_mesh"), &SpineSprite3D::generate_triangle_mesh);

	ADD_SIGNAL(MethodInfo("animation_started", PropertyInfo(Variant::OBJECT, "spine_sprite", PROPERTY_HINT_TYPE_STRING, "SpineSprite3D"),
						  PropertyInfo(Variant::OBJECT, "animation_state", PROPERTY_HINT_TYPE_STRING, "SpineAnimationState"),
						  PropertyInfo(Variant::OBJECT, "track_entry", PROPERTY_HINT_TYPE_STRING, "SpineTrackEntry")));
	ADD_SIGNAL(MethodInfo("animation_interrupted", PropertyInfo(Variant::OBJECT, "spine_sprite", PROPERTY_HINT_TYPE_STRING, "SpineSprite3D"),
						  PropertyInfo(Variant::OBJECT, "animation_state", PROPERTY_HINT_TYPE_STRING, "SpineAnimationState"),
						  PropertyInfo(Variant::OBJECT, "track_entry", PROPERTY_HINT_TYPE_STRING, "SpineTrackEntry")));
	ADD_SIGNAL(MethodInfo("animation_ended", PropertyInfo(Variant::OBJECT, "spine_sprite", PROPERTY_HINT_TYPE_STRING, "SpineSprite3D"),
						  PropertyInfo(Variant::OBJECT, "animation_state", PROPERTY_HINT_TYPE_STRING, "SpineAnimationState"),
						  PropertyInfo(Variant::OBJECT, "track_entry", PROPERTY_HINT_TYPE_STRING, "SpineTrackEntry")));
	ADD_SIGNAL(MethodInfo("animation_completed", PropertyInfo(Variant::OBJECT, "spine_sprite", PROPERTY_HINT_TYPE_STRING, "SpineSprite3D"),
						  PropertyInfo(Variant::OBJECT, "animation_state", PROPERTY_HINT_TYPE_STRING, "SpineAnimationState"),
						  PropertyInfo(Variant::OBJECT, "track_entry", PROPERTY_HINT_TYPE_STRING, "SpineTrackEntry")));
	ADD_SIGNAL(MethodInfo("animation_disposed", PropertyInfo(Variant::OBJECT, "spine_sprite", PROPERTY_HINT_TYPE_STRING, "SpineSprite3D"),
						  PropertyInfo(Variant::OBJECT, "animation_state", PROPERTY_HINT_TYPE_STRING, "SpineAnimationState"),
						  PropertyInfo(Variant::OBJECT, "track_entry", PROPERTY_HINT_TYPE_STRING, "SpineTrackEntry")));
	ADD_SIGNAL(MethodInfo("animation_event", PropertyInfo(Variant::OBJECT, "spine_sprite", PROPERTY_HINT_TYPE_STRING, "SpineSprite3D"),
						  PropertyInfo(Variant::OBJECT, "animation_state", PROPERTY_HINT_TYPE_STRING, "SpineAnimationState"),
						  PropertyInfo(Variant::OBJECT, "track_entry", PROPERTY_HINT_TYPE_STRING, "SpineTrackEntry"),
						  PropertyInfo(Variant::OBJECT, "event", PROPERTY_HINT_TYPE_STRING, "SpineEvent")));
	ADD_SIGNAL(MethodInfo("before_animation_state_update", PropertyInfo(Variant::OBJECT, "spine_sprite", PROPERTY_HINT_TYPE_STRING, "SpineSprite3D")));
	ADD_SIGNAL(MethodInfo("before_animation_state_apply", PropertyInfo(Variant::OBJECT, "spine_sprite", PROPERTY_HINT_TYPE_STRING, "SpineSprite3D")));
	ADD_SIGNAL(MethodInfo("before_world_transforms_change", PropertyInfo(Variant::OBJECT, "spine_sprite", PROPERTY_HINT_TYPE_STRING, "SpineSprite3D")));
	ADD_SIGNAL(MethodInfo("world_transforms_changed", PropertyInfo(Variant::OBJECT, "spine_sprite", PROPERTY_HINT_TYPE_STRING, "SpineSprite3D")));
	ADD_SIGNAL(MethodInfo("_internal_spine_objects_invalidated"));

	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "skeleton_data_res", PropertyHint::PROPERTY_HINT_RESOURCE_TYPE, "SpineSkeletonDataResource"),
				 "set_skeleton_data_res", "get_skeleton_data_res");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "update_mode", PROPERTY_HINT_ENUM, "Process,Physics,Manual"), "set_update_mode", "get_update_mode");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "flip_h"), "set_flip_h", "is_flipped_h");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "flip_v"), "set_flip_v", "is_flipped_v");
	ADD_PROPERTY(PropertyInfo(Variant::COLOR, "modulate"), "set_modulate", "get_modulate");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "pixel_size", PROPERTY_HINT_RANGE, "0.0001,128,0.0000001,suffix:m"), "set_pixel_size", "get_pixel_size");
	ADD_GROUP("Flags", "");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "billboard", PROPERTY_HINT_ENUM, "Disabled,Enabled,Y-Billboard"), "set_billboard_mode", "get_billboard_mode");
	ADD_PROPERTYI(PropertyInfo(Variant::BOOL, "transparent"), "set_draw_flag", "get_draw_flag", FLAG_TRANSPARENT);
	ADD_PROPERTYI(PropertyInfo(Variant::BOOL, "shaded"), "set_draw_flag", "get_draw_flag", FLAG_SHADED);
	ADD_PROPERTYI(PropertyInfo(Variant::BOOL, "double_sided"), "set_draw_flag", "get_draw_flag", FLAG_DOUBLE_SIDED);
	ADD_PROPERTYI(PropertyInfo(Variant::BOOL, "no_depth_test"), "set_draw_flag", "get_draw_flag", FLAG_DISABLE_DEPTH_TEST);
	ADD_PROPERTYI(PropertyInfo(Variant::BOOL, "fixed_size"), "set_draw_flag", "get_draw_flag", FLAG_FIXED_SIZE);
	ADD_PROPERTY(PropertyInfo(Variant::INT, "texture_filter", PROPERTY_HINT_ENUM, "Nearest,Linear,Nearest Mipmap,Linear Mipmap,Nearest Mipmap Anisotropic,Linear Mipmap Anisotropic"), "set_texture_filter", "get_texture_filter");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "render_priority", PROPERTY_HINT_RANGE, "-128,127,1"), "set_sprite_render_priority", "get_sprite_render_priority");

	BIND_ENUM_CONSTANT(FLAG_TRANSPARENT);
	BIND_ENUM_CONSTANT(FLAG_SHADED);
	BIND_ENUM_CONSTANT(FLAG_DOUBLE_SIDED);
	BIND_ENUM_CONSTANT(FLAG_DISABLE_DEPTH_TEST);
	BIND_ENUM_CONSTANT(FLAG_FIXED_SIZE);
	BIND_ENUM_CONSTANT(FLAG_MAX);

	ADD_GROUP("Materials", "");
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "normal_material", PROPERTY_HINT_RESOURCE_TYPE, "Material"), "set_normal_material", "get_normal_material");
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "additive_material", PROPERTY_HINT_RESOURCE_TYPE, "Material"), "set_additive_material", "get_additive_material");
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "multiply_material", PROPERTY_HINT_RESOURCE_TYPE, "Material"), "set_multiply_material", "get_multiply_material");
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "screen_material", PROPERTY_HINT_RESOURCE_TYPE, "Material"), "set_screen_material", "get_screen_material");

	ADD_GROUP("Debug", "");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "root"), "set_debug_root", "get_debug_root");
	ADD_PROPERTY(PropertyInfo(Variant::COLOR, "root_color"), "set_debug_root_color", "get_debug_root_color");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "bones"), "set_debug_bones", "get_debug_bones");
	ADD_PROPERTY(PropertyInfo(Variant::COLOR, "bones_color"), "set_debug_bones_color", "get_debug_bones_color");
	ADD_PROPERTY(PropertyInfo(VARIANT_FLOAT, "bones_thickness"), "set_debug_bones_thickness", "get_debug_bones_thickness");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "regions"), "set_debug_regions", "get_debug_regions");
	ADD_PROPERTY(PropertyInfo(Variant::COLOR, "regions_color"), "set_debug_regions_color", "get_debug_regions_color");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "meshes"), "set_debug_meshes", "get_debug_meshes");
	ADD_PROPERTY(PropertyInfo(Variant::COLOR, "meshes_color"), "set_debug_meshes_color", "get_debug_meshes_color");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "bounding_boxes"), "set_debug_bounding_boxes", "get_debug_bounding_boxes");
	ADD_PROPERTY(PropertyInfo(Variant::COLOR, "bounding_boxes_color"), "set_debug_bounding_boxes_color", "get_debug_bounding_boxes_color");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "paths"), "set_debug_paths", "get_debug_paths");
	ADD_PROPERTY(PropertyInfo(Variant::COLOR, "paths_color"), "set_debug_paths_color", "get_debug_paths_color");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "clipping"), "set_debug_clipping", "get_debug_clipping");
	ADD_PROPERTY(PropertyInfo(Variant::COLOR, "paths_clipping"), "set_debug_clipping_color", "get_debug_clipping_color");

	ADD_GROUP("Preview", "");
}

SpineSprite3D::SpineSprite3D()
	: update_mode(SpineConstant::UpdateMode_Process), time_scale(1.0f), preview_skin(""), preview_animation(SPINE_PREVIEW_NONE),
	  preview_frame(false), preview_time(0), atlas_textures_pending_refresh(false), debug_mesh_instance(nullptr), skeleton_clipper(nullptr),
#if defined(TOOLS_ENABLED) && !defined(SPINE_GODOT_EXTENSION)
	  editor_filesystem_bound(false),
#endif
	  modified_bones(false), ready_notified(false), flip_h(false), flip_v(false), flip_origin_x(0.0f), flip_origin_y(0.0f), flip_origin_valid(false),
	  modulate(Color(1, 1, 1, 1)), pixel_size(0.1), sprite_render_priority(0), billboard_mode(BaseMaterial3D::BILLBOARD_DISABLED),
	  texture_filter(BaseMaterial3D::TEXTURE_FILTER_LINEAR_WITH_MIPMAPS) {
	for (int i = 0; i < FLAG_MAX; i++) {
		draw_flags[i] = i == FLAG_TRANSPARENT || i == FLAG_DOUBLE_SIDED;
	}
	skeleton_clipper = new spine::SkeletonClipping();
	debug_root = false;
	debug_root_color = Color(1, 1, 1, 0.5);
	debug_bones = false;
	debug_bones_color = Color(1, 1, 0, 0.5);
	debug_bones_thickness = 5;
	debug_regions = false;
	debug_regions_color = Color(0, 0, 1, 0.5);
	debug_meshes = false;
	debug_meshes_color = Color(0, 0, 1, 0.5);
	debug_bounding_boxes = false;
	debug_bounding_boxes_color = Color(0, 1, 0, 0.5);
	debug_paths = false;
	debug_paths_color = Color::hex(0xff7f0077);
	debug_clipping = false;
	debug_clipping_color = Color(1, 0, 0, 0.5);
	atlas_texture_refresh_callable = callable_mp(this, &SpineSprite3D::visual_settings_changed);
	SpineSprite3DStatics::instance().sprite_count++;
}

SpineSprite3D::~SpineSprite3D() {
	disconnect_skeleton_data_res_signals();
	if (animation_state.is_valid() && animation_state->get_spine_object()) {
		animation_state->get_spine_object()->setListener((spine::AnimationStateListenerObject *) nullptr);
	}
	disconnect_atlas_texture_refresh();
	unbind_editor_import_refresh();
	remove_meshes();
	skeleton.unref();
	animation_state.unref();
	pick_triangle_mesh.unref();
	if (debug_mesh_instance) {
		remove_child(debug_mesh_instance);
		memdelete(debug_mesh_instance);
		debug_mesh_instance = nullptr;
	}
	delete skeleton_clipper;
	SpineSprite3DStatics::instance().sprite_count--;
}

void SpineSprite3D::_notification(int what) {
	switch (what) {
		case NOTIFICATION_EXIT_TREE:
			ready_notified = false;
			disconnect_skeleton_data_res_signals();
			disconnect_atlas_texture_refresh();
			unbind_editor_import_refresh();
			if (animation_state.is_valid() && animation_state->get_spine_object()) {
				animation_state->get_spine_object()->setListener((spine::AnimationStateListenerObject *) nullptr);
			}
			skeleton.unref();
			animation_state.unref();
			suspend_rendering();
			break;
		case NOTIFICATION_ENTER_TREE:
			connect_skeleton_data_res_signals();
			if (skeleton_data_res.is_valid() && skeleton_data_res->is_skeleton_data_loaded() && !skeleton.is_valid()) {
				if (ready_notified) {
					rebuild_spine_objects();
				}
			} else if (skeleton.is_valid()) {
				schedule_display_refresh();
			}
			break;
		case NOTIFICATION_VISIBILITY_CHANGED:
			if (is_visible_in_tree() && skeleton.is_valid()) {
				schedule_display_refresh();
			}
			break;
		case NOTIFICATION_READY:
			ready_notified = true;
			set_process_internal(update_mode == SpineConstant::UpdateMode_Process);
			set_physics_process_internal(update_mode == SpineConstant::UpdateMode_Physics);
			bind_editor_import_refresh();
			connect_skeleton_data_res_signals();
			if (skeleton_data_res.is_valid() && skeleton_data_res->is_skeleton_data_loaded() && !skeleton.is_valid()) {
				rebuild_spine_objects();
			} else if (skeleton.is_valid()) {
				schedule_display_refresh();
			}
			break;
		case NOTIFICATION_INTERNAL_PROCESS:
			if (update_mode == SpineConstant::UpdateMode_Process) update_skeleton(get_process_delta_time());
			break;
		case NOTIFICATION_INTERNAL_PHYSICS_PROCESS:
			if (update_mode == SpineConstant::UpdateMode_Physics) update_skeleton(get_physics_process_delta_time());
			break;
		default:
			break;
	}
}

void SpineSprite3D::set_skeleton_data_res(const Ref<SpineSkeletonDataResource> &resource) {
	if (skeleton_data_res == resource) {
		return;
	}
	disconnect_skeleton_data_res_signals();
	teardown_spine_objects();
	skeleton_data_res = resource;
	connect_skeleton_data_res_signals();
	schedule_skeleton_rebuild();
}

Ref<SpineSkeletonDataResource> SpineSprite3D::get_skeleton_data_res() {
	return skeleton_data_res;
}

Ref<SpineSkeleton> SpineSprite3D::get_skeleton() {
	return skeleton;
}

Ref<SpineAnimationState> SpineSprite3D::get_animation_state() {
	return animation_state;
}

void SpineSprite3D::connect_skeleton_data_res_signals() {
	if (!skeleton_data_res.is_valid()) {
		return;
	}
	const Callable teardown_callable = callable_mp(this, &SpineSprite3D::teardown_spine_objects);
	const Callable rebuild_callable = callable_mp(this, &SpineSprite3D::schedule_skeleton_rebuild);
	if (!skeleton_data_res->is_connected(SNAME("_internal_spine_objects_invalidated"), teardown_callable)) {
		skeleton_data_res->connect(SNAME("_internal_spine_objects_invalidated"), teardown_callable);
	}
	if (!skeleton_data_res->is_connected(SNAME("skeleton_data_changed"), rebuild_callable)) {
		skeleton_data_res->connect(SNAME("skeleton_data_changed"), rebuild_callable);
	}
}

void SpineSprite3D::disconnect_skeleton_data_res_signals() {
	if (!skeleton_data_res.is_valid()) {
		return;
	}
	const Callable teardown_callable = callable_mp(this, &SpineSprite3D::teardown_spine_objects);
	const Callable rebuild_callable = callable_mp(this, &SpineSprite3D::schedule_skeleton_rebuild);
	if (skeleton_data_res->is_connected(SNAME("_internal_spine_objects_invalidated"), teardown_callable)) {
		skeleton_data_res->disconnect(SNAME("_internal_spine_objects_invalidated"), teardown_callable);
	}
	if (skeleton_data_res->is_connected(SNAME("skeleton_data_changed"), rebuild_callable)) {
		skeleton_data_res->disconnect(SNAME("skeleton_data_changed"), rebuild_callable);
	}
}

void SpineSprite3D::teardown_spine_objects() {
	if (!is_inside_tree() || is_queued_for_deletion()) {
		suspend_rendering();
		if (animation_state.is_valid() && animation_state->get_spine_object()) {
			animation_state->get_spine_object()->setListener((spine::AnimationStateListenerObject *) nullptr);
		}
		disconnect_atlas_texture_refresh();
		skeleton.unref();
		animation_state.unref();
		return;
	}
	suspend_rendering();
	if (animation_state.is_valid() && animation_state->get_spine_object()) {
		animation_state->get_spine_object()->setListener((spine::AnimationStateListenerObject *) nullptr);
	}
	disconnect_atlas_texture_refresh();
	skeleton.unref();
	animation_state.unref();
	emit_signal(SNAME("_internal_spine_objects_invalidated"));
#ifdef TOOLS_ENABLED
	if (is_inside_tree() && !is_queued_for_deletion()) {
		call_deferred(SNAME("update_gizmos"));
	}
#endif
}

bool SpineSprite3D::is_renderable() const {
	return skeleton_data_res.is_valid() && skeleton_data_res->get_atlas_res().is_valid() && skeleton_data_res->get_skeleton_file_res().is_valid() &&
		   skeleton_data_res->is_skeleton_data_loaded();
}

void SpineSprite3D::suspend_rendering() {
	atlas_textures_pending_refresh = false;
	pick_triangle_mesh.unref();
	for (int i = 0; i < mesh_instances.size(); ++i) {
		SpineMesh3D *mesh_instance = mesh_instances[i];
		if (mesh_instance && !mesh_instance->is_queued_for_deletion()) {
			mesh_instance->clear_mesh_surface();
		}
	}
	remove_meshes();
	if (debug_mesh_instance && !debug_mesh_instance->is_queued_for_deletion()) {
		debug_mesh_instance->set_visible(false);
	}
}

void SpineSprite3D::teardown_mesh_children() {
	if (is_queued_for_deletion() || !is_inside_tree()) {
		return;
	}
	suspend_rendering();
#ifdef TOOLS_ENABLED
	call_deferred(SNAME("update_gizmos"));
#endif
}

void SpineSprite3D::schedule_skeleton_rebuild() {
	if (!is_inside_tree() || is_queued_for_deletion()) {
		return;
	}
	call_deferred(SNAME("rebuild_spine_objects"));
}

void SpineSprite3D::rebuild_spine_objects() {
	if (is_queued_for_deletion() || !is_inside_tree()) {
		return;
	}

	suspend_rendering();

	if (!is_renderable()) {
		call_deferred(SNAME("notify_property_list_changed"));
		return;
	}

	Vector<SpineSprite3DSavedTrack> saved_tracks;
	spine_sprite3d_save_animation_tracks(animation_state, saved_tracks);

	atlas_textures_pending_refresh = false;
	skeleton = Ref<SpineSkeleton>(memnew(SpineSkeleton));
	skeleton->set_spine_sprite(this);
	animation_state = Ref<SpineAnimationState>(memnew(SpineAnimationState));
	animation_state->set_spine_sprite(this);
	if (!animation_state->get_spine_object()) {
		ERR_PRINT("Spine: animation_state native object is null after set_spine_sprite, aborting rebuild.");
		skeleton.unref();
		animation_state.unref();
		call_deferred(SNAME("notify_property_list_changed"));
		return;
	}
	animation_state->get_spine_object()->setListener(this);
	spine_sprite3d_restore_animation_tracks(animation_state, saved_tracks);
	animation_state->update(0);
	animation_state->apply(skeleton);
	skeleton->update_world_transform(SpineConstant::Physics_Update);
	generate_meshes_for_slots(skeleton);
	refresh_atlas_page_textures();
	connect_atlas_texture_refresh();
	update_meshes(skeleton);
	schedule_display_refresh();

	if (update_mode == SpineConstant::UpdateMode_Process) {
		_notification(NOTIFICATION_INTERNAL_PROCESS);
	} else if (update_mode == SpineConstant::UpdateMode_Physics) {
		_notification(NOTIFICATION_INTERNAL_PHYSICS_PROCESS);
	}
#ifdef TOOLS_ENABLED
	pick_triangle_mesh.unref();
	call_deferred(SNAME("update_gizmos"));
#endif

	call_deferred(SNAME("notify_property_list_changed"));

#ifdef TOOLS_ENABLED
	if (Engine::get_singleton()->is_editor_hint()) {
		preview_skin = spine_resolve_preview_skin(skeleton_data_res, preview_skin);
		update_preview_animation(this, preview_skin, preview_animation, preview_frame, preview_time);
	}
#endif
}

void SpineSprite3D::on_skeleton_data_changed() {
	schedule_skeleton_rebuild();
}

void SpineSprite3D::refresh_atlas_page_textures() {
	if (!skeleton_data_res.is_valid()) {
		return;
	}
	Ref<SpineAtlasResource> atlas = skeleton_data_res->get_atlas_res();
	if (!atlas.is_valid()) {
		return;
	}
	atlas->reload_page_textures();
}

void SpineSprite3D::disconnect_atlas_texture_refresh() {
	if (!connected_atlas_res.is_valid()) {
		return;
	}
	Array textures = connected_atlas_res->get_textures();
	for (int i = 0; i < textures.size(); i++) {
		Ref<Texture2D> texture = textures[i];
		if (!texture.is_valid()) {
			continue;
		}
		if (texture->is_connected(SNAME("changed"), atlas_texture_refresh_callable)) {
			texture->disconnect(SNAME("changed"), atlas_texture_refresh_callable);
		}
		if (texture->has_signal(SNAME("replaced")) && texture->is_connected(SNAME("replaced"), atlas_texture_refresh_callable)) {
			texture->disconnect(SNAME("replaced"), atlas_texture_refresh_callable);
		}
	}
	connected_atlas_res.unref();
}

void SpineSprite3D::connect_atlas_texture_refresh() {
	disconnect_atlas_texture_refresh();
	if (!skeleton_data_res.is_valid() || !is_inside_tree() || is_queued_for_deletion()) {
		return;
	}
	Ref<SpineAtlasResource> atlas = skeleton_data_res->get_atlas_res();
	if (!atlas.is_valid()) {
		return;
	}
	connected_atlas_res = atlas;
	Array textures = atlas->get_textures();
	for (int i = 0; i < textures.size(); i++) {
		Ref<Texture2D> texture = textures[i];
		if (!texture.is_valid()) {
			continue;
		}
		if (!texture->is_connected(SNAME("changed"), atlas_texture_refresh_callable)) {
			texture->connect(SNAME("changed"), atlas_texture_refresh_callable);
		}
		if (texture->has_signal(SNAME("replaced"))) {
			if (!texture->is_connected(SNAME("replaced"), atlas_texture_refresh_callable)) {
				texture->connect(SNAME("replaced"), atlas_texture_refresh_callable);
			}
		}
	}
}

void SpineSprite3D::schedule_display_refresh() {
	if (!is_inside_tree() || is_queued_for_deletion()) {
		return;
	}
	call_deferred(SNAME("visual_settings_changed"));
}

Ref<Texture2D> SpineSprite3D::resolve_albedo_texture(SpineRendererObject *p_renderer_object) {
	Ref<Texture2D> albedo_texture = texture_ref_from_renderer_object(p_renderer_object);
	if (is_albedo_texture_ready(albedo_texture)) {
		return albedo_texture;
	}

	if (!skeleton_data_res.is_valid()) {
		return Ref<Texture2D>();
	}
	Ref<SpineAtlasResource> atlas = skeleton_data_res->get_atlas_res();
	if (!atlas.is_valid()) {
		return Ref<Texture2D>();
	}

	if (!atlas_textures_pending_refresh) {
		atlas_textures_pending_refresh = true;
		atlas->reload_page_textures();
		connect_atlas_texture_refresh();
		albedo_texture = texture_ref_from_renderer_object(p_renderer_object);
		if (is_albedo_texture_ready(albedo_texture)) {
			atlas_textures_pending_refresh = false;
			return albedo_texture;
		}
		schedule_display_refresh();
	}
	return Ref<Texture2D>();
}

void SpineSprite3D::bind_editor_import_refresh() {
#if defined(TOOLS_ENABLED) && !defined(SPINE_GODOT_EXTENSION)
	Engine *engine = Engine::get_singleton();
	if (!engine || !engine->is_editor_hint()) {
		return;
	}
	EditorFileSystem *filesystem = EditorFileSystem::get_singleton();
	if (!filesystem) {
		return;
	}
	if (!editor_filesystem_bound) {
		editor_filesystem_callable = callable_mp(this, &SpineSprite3D::on_editor_filesystem_changed);
		editor_filesystem_bound = true;
	}
	if (!filesystem->is_connected(SNAME("filesystem_changed"), editor_filesystem_callable)) {
		filesystem->connect(SNAME("filesystem_changed"), editor_filesystem_callable);
	}
	if (!filesystem->is_connected(SNAME("resources_reimported"), editor_filesystem_callable)) {
		filesystem->connect(SNAME("resources_reimported"), editor_filesystem_callable);
	}
#endif
}

void SpineSprite3D::unbind_editor_import_refresh() {
#if defined(TOOLS_ENABLED) && !defined(SPINE_GODOT_EXTENSION)
	if (!editor_filesystem_bound) {
		return;
	}
	EditorFileSystem *filesystem = EditorFileSystem::get_singleton();
	if (filesystem) {
		if (filesystem->is_connected(SNAME("filesystem_changed"), editor_filesystem_callable)) {
			filesystem->disconnect(SNAME("filesystem_changed"), editor_filesystem_callable);
		}
		if (filesystem->is_connected(SNAME("resources_reimported"), editor_filesystem_callable)) {
			filesystem->disconnect(SNAME("resources_reimported"), editor_filesystem_callable);
		}
	}
	editor_filesystem_bound = false;
#endif
}

void SpineSprite3D::on_editor_filesystem_changed() {
#if defined(TOOLS_ENABLED) && !defined(SPINE_GODOT_EXTENSION)
	if (!is_inside_tree() || is_queued_for_deletion()) {
		return;
	}
	EditorFileSystem *filesystem = EditorFileSystem::get_singleton();
	if (filesystem && filesystem->is_scanning()) {
		return;
	}
#endif
	refresh_display();
}

void SpineSprite3D::generate_meshes_for_slots(Ref<SpineSkeleton> skeleton_ref) {
	auto skeleton_obj = skeleton_ref->get_spine_object();
	auto &statics = SpineSprite3DStatics::instance();
	for (int i = 0, n = (int)skeleton_obj->getSlots().size(); i < n; i++) {
		auto mesh_instance = memnew(SpineMesh3D);
		mesh_instance->set_material(statics.default_materials[spine::BlendMode_Normal]);
		mesh_instance->set_sorting_use_aabb_center(false);
		mesh_instance->set_sorting_offset(0.0f);
		add_child(mesh_instance, false, INTERNAL_MODE_BACK);
		mesh_instance->set_owner(this);
#ifdef TOOLS_ENABLED
		mesh_instance->set_meta("_edit_lock_", true);
#endif
		mesh_instances.push_back(mesh_instance);
	}
	pick_triangle_mesh.unref();
}

void SpineSprite3D::remove_meshes() {
	for (int i = 0; i < mesh_instances.size(); ++i) {
		SpineMesh3D *mesh_instance = mesh_instances[i];
		if (!mesh_instance || mesh_instance->is_queued_for_deletion()) {
			continue;
		}
		if (mesh_instance->get_parent() == this) {
			remove_child(mesh_instance);
		}
		memdelete(mesh_instance);
	}
	mesh_instances.clear();
}

void SpineSprite3D::update_skeleton(float delta) {
	if (!is_renderable() || !skeleton.is_valid() || !skeleton->get_spine_object() || !animation_state.is_valid() ||
		!animation_state->get_spine_object()) {
		return;
	}

	emit_signal(SNAME("before_animation_state_update"), this);
	animation_state->update(delta * time_scale);
	if (!is_visible_in_tree()) return;
	emit_signal(SNAME("before_animation_state_apply"), this);
	animation_state->apply(skeleton);
	emit_signal(SNAME("before_world_transforms_change"), this);
	skeleton->update(delta * time_scale);
	skeleton->update_world_transform(SpineConstant::Physics_Update);
	modified_bones = false;
	emit_signal(SNAME("world_transforms_changed"), this);
	if (modified_bones) skeleton->update_world_transform(SpineConstant::Physics_Update);
	update_meshes(skeleton);
	if (debug_mesh_instance) {
		move_child(debug_mesh_instance, -1);
	}
	draw_debug();
}

void SpineSprite3D::update_meshes(Ref<SpineSkeleton> skeleton_ref) {
	if (!is_renderable() || !skeleton_ref.is_valid() || !skeleton_ref->get_spine_object()) {
		return;
	}

	pick_triangle_mesh.unref();
	auto &statics = SpineSprite3DStatics::instance();
	spine::Skeleton *skeleton_obj = skeleton_ref->get_spine_object();
	const int slot_count = (int)skeleton_obj->getSlots().size();
	if (mesh_instances.size() != slot_count) {
		return;
	}

	update_flip_origin(skeleton_obj);

	for (int i = 0; i < slot_count; ++i) {
		spine::Slot *slot = skeleton_obj->getDrawOrder().getAppliedPose()[i];
		spine::Attachment *attachment = slot->getAppliedPose().getAttachment();
		SpineMesh3D *mesh_instance = mesh_instances[i];
		mesh_instance->set_position(Vector3(0, 0, 0));
		mesh_instance->set_sorting_offset((float)i * SPINE_SLOT_SORTING_OFFSET_STEP);

		if (!attachment || !slot->getBone().isActive()) {
			mesh_instance->update_mesh(PackedVector3Array(), PackedVector2Array(), PackedColorArray(), PackedInt32Array(), Ref<Texture2D>(), i);
			skeleton_clipper->clipEnd(*slot);
			continue;
		}

		spine::Color skeleton_color = skeleton_obj->getColor();
		spine::Color slot_color = slot->getAppliedPose().getColor();
		spine::Color tint(skeleton_color.r * slot_color.r * modulate.r, skeleton_color.g * slot_color.g * modulate.g,
						  skeleton_color.b * slot_color.b * modulate.b, skeleton_color.a * slot_color.a * modulate.a);
		SpineRendererObject *slot_renderer_object = nullptr;
		spine::Array<float> *scratch_vertices = &statics.scratch_vertices;
		spine::Array<float> *scratch_uvs = nullptr;
		spine::Array<unsigned short> *scratch_indices = nullptr;

		if (attachment->getRTTI().isExactly(spine::RegionAttachment::rtti)) {
			auto region = (spine::RegionAttachment *)attachment;
			auto &sequence = region->getSequence();
			int sequence_index = sequence.resolveIndex(slot->getAppliedPose());
			scratch_vertices->setSize(8, 0);
			region->computeWorldVertices(*slot, sequence.getOffsets(sequence_index).buffer(), scratch_vertices->buffer(), 0);
			slot_renderer_object = renderer_object_from_sequence(sequence, slot->getAppliedPose());
			if (!slot_renderer_object) {
				mesh_instance->update_mesh(PackedVector3Array(), PackedVector2Array(), PackedColorArray(), PackedInt32Array(), Ref<Texture2D>(), i);
				skeleton_clipper->clipEnd(*slot);
				continue;
			}
			scratch_uvs = &sequence.getUVs(sequence_index);
			scratch_indices = &statics.quad_indices;
			auto &attachment_color = region->getColor();
			tint.r *= attachment_color.r;
			tint.g *= attachment_color.g;
			tint.b *= attachment_color.b;
			tint.a *= attachment_color.a;
		} else if (attachment->getRTTI().isExactly(spine::MeshAttachment::rtti)) {
			auto mesh = (spine::MeshAttachment *)attachment;
			auto &sequence = mesh->getSequence();
			int sequence_index = sequence.resolveIndex(slot->getAppliedPose());
			scratch_vertices->setSize(mesh->getWorldVerticesLength(), 0);
			mesh->computeWorldVertices(*skeleton_obj, *slot, 0, mesh->getWorldVerticesLength(), scratch_vertices->buffer(), 0, 2);
			slot_renderer_object = renderer_object_from_sequence(sequence, slot->getAppliedPose());
			if (!slot_renderer_object) {
				mesh_instance->update_mesh(PackedVector3Array(), PackedVector2Array(), PackedColorArray(), PackedInt32Array(), Ref<Texture2D>(), i);
				skeleton_clipper->clipEnd(*slot);
				continue;
			}
			scratch_uvs = &sequence.getUVs(sequence_index);
			scratch_indices = &mesh->getTriangles();
			auto &attachment_color = mesh->getColor();
			tint.r *= attachment_color.r;
			tint.g *= attachment_color.g;
			tint.b *= attachment_color.b;
			tint.a *= attachment_color.a;
		} else if (attachment->getRTTI().isExactly(spine::ClippingAttachment::rtti)) {
			skeleton_clipper->clipStart(*skeleton_obj, *slot, (spine::ClippingAttachment *)attachment);
			continue;
		} else {
			skeleton_clipper->clipEnd(*slot);
			continue;
		}

		if (skeleton_clipper->isClipping()) {
			skeleton_clipper->clipTriangles(*scratch_vertices, *scratch_indices, *scratch_uvs, 2);
			if (skeleton_clipper->getClippedTriangles().size() == 0) {
				skeleton_clipper->clipEnd(*slot);
				continue;
			}
			scratch_vertices = &skeleton_clipper->getClippedVertices();
			scratch_uvs = &skeleton_clipper->getClippedUVs();
			scratch_indices = &skeleton_clipper->getClippedTriangles();
		}

		if (scratch_indices->size() > 0) {
			const int num_vertices = (int)scratch_vertices->size() / 2;
			const int num_indices = (int)scratch_indices->size();
			scratch_mesh_vertices.resize(num_vertices);
			scratch_mesh_uvs.resize(num_vertices);
			scratch_mesh_colors.resize(num_vertices);
			for (int j = 0; j < num_vertices; j++) {
				const float x = scratch_vertices->buffer()[j * 2];
				const float y = scratch_vertices->buffer()[j * 2 + 1];
				scratch_mesh_vertices.set(j, spine_vertex_to_local(x, y, 0));
				scratch_mesh_uvs.set(j, Vector2(scratch_uvs->buffer()[j * 2], scratch_uvs->buffer()[j * 2 + 1]));
				scratch_mesh_colors.set(j, Color(tint.r, tint.g, tint.b, tint.a));
			}
			scratch_mesh_indices.resize(num_indices);
			for (int j = 0; j < num_indices; ++j) {
				scratch_mesh_indices.set(j, scratch_indices->buffer()[j]);
			}

			spine::BlendMode blend_mode = slot->getData().getBlendMode();
			Ref<Material> slot_material;
			switch (blend_mode) {
				case spine::BlendMode_Normal: slot_material = normal_material; break;
				case spine::BlendMode_Additive: slot_material = additive_material; break;
				case spine::BlendMode_Multiply: slot_material = multiply_material; break;
				case spine::BlendMode_Screen: slot_material = screen_material; break;
			}
			if (!slot_material.is_valid()) {
				slot_material = statics.default_materials[blend_mode];
			}
			if (mesh_instance->cached_slot_material != slot_material) {
				mesh_instance->set_material(slot_material);
				mesh_instance->cached_slot_material = slot_material;
			}

			Ref<Texture2D> albedo_texture = resolve_albedo_texture(slot_renderer_object);
			mesh_instance->update_mesh(scratch_mesh_vertices, scratch_mesh_uvs, scratch_mesh_colors, scratch_mesh_indices, albedo_texture, i);
		}
		skeleton_clipper->clipEnd(*slot);
	}
	skeleton_clipper->clipEnd();
}

void SpineSprite3D::update_flip_origin(spine::Skeleton *skeleton_obj) {
	flip_origin_valid = false;
	if (!skeleton_obj || (!flip_h && !flip_v)) {
		return;
	}

	float bounds_x = 0.0f;
	float bounds_y = 0.0f;
	float bounds_w = 0.0f;
	float bounds_h = 0.0f;
	spine::SkeletonClipping bounds_clipper;
	auto &statics = SpineSprite3DStatics::instance();
	skeleton_obj->getBounds(bounds_x, bounds_y, bounds_w, bounds_h, statics.scratch_vertices, &bounds_clipper);
	flip_origin_x = bounds_x + bounds_w * 0.5f;
	flip_origin_y = bounds_y + bounds_h * 0.5f;
	flip_origin_valid = true;
}

Vector3 SpineSprite3D::spine_vertex_to_local(float x, float y, int draw_order) const {
	if (flip_origin_valid) {
		if (flip_h) {
			x = flip_origin_x + (flip_origin_x - x);
		}
		if (flip_v) {
			y = flip_origin_y + (flip_origin_y - y);
		}
	}
	x *= pixel_size;
	y *= pixel_size;
	return Vector3(x, -y, (float)draw_order * SPINE_SLOT_SORT_Z_STEP);
}

void SpineSprite3D::visual_settings_changed() {
	if (!is_inside_tree() || is_queued_for_deletion()) {
		return;
	}
	atlas_textures_pending_refresh = false;
	pick_triangle_mesh.unref();
	if (!is_renderable() || !skeleton.is_valid()) {
#ifdef TOOLS_ENABLED
		call_deferred(SNAME("update_gizmos"));
#endif
		return;
	}
	for (int i = 0; i < mesh_instances.size(); i++) {
		mesh_instances[i]->surface_material_dirty = true;
		mesh_instances[i]->instance_refresh_needed = true;
		mesh_instances[i]->last_vertex_count = 0;
		mesh_instances[i]->last_index_count = 0;
		mesh_instances[i]->pick_vertices.clear();
		mesh_instances[i]->pick_indices.clear();
		mesh_instances[i]->cached_surface_material.unref();
		mesh_instances[i]->cached_albedo_texture.unref();
	}
	update_meshes(skeleton);
#ifdef TOOLS_ENABLED
	call_deferred(SNAME("update_gizmos"));
#endif
}

void SpineSprite3D::refresh_display() {
	if (!is_renderable()) {
		suspend_rendering();
		return;
	}
	refresh_atlas_page_textures();
	connect_atlas_texture_refresh();
	visual_settings_changed();
}

void SpineSprite3D::configure_slot_material(StandardMaterial3D *p_material, int draw_order) const {
	if (!p_material) {
		return;
	}

	p_material->set_shading_mode(draw_flags[FLAG_SHADED] ? BaseMaterial3D::SHADING_MODE_PER_PIXEL : BaseMaterial3D::SHADING_MODE_UNSHADED);
	p_material->set_cull_mode(draw_flags[FLAG_DOUBLE_SIDED] ? BaseMaterial3D::CULL_DISABLED : BaseMaterial3D::CULL_BACK);
	p_material->set_transparency(draw_flags[FLAG_TRANSPARENT] ? BaseMaterial3D::TRANSPARENCY_ALPHA : BaseMaterial3D::TRANSPARENCY_DISABLED);
	p_material->set_depth_draw_mode(draw_flags[FLAG_DISABLE_DEPTH_TEST] ? BaseMaterial3D::DEPTH_DRAW_DISABLED : BaseMaterial3D::DEPTH_DRAW_OPAQUE_ONLY);
	p_material->set_billboard_mode(billboard_mode);
	p_material->set_flag(BaseMaterial3D::FLAG_BILLBOARD_KEEP_SCALE, draw_flags[FLAG_FIXED_SIZE]);
	p_material->set_texture_filter(texture_filter);
	// One priority per sprite so world depth orders separate SpineSprite3D nodes.
	// Slot draw order uses sorting_offset (view axis), not render_priority.
	(void)draw_order;
	p_material->set_render_priority(sprite_render_priority);
}

void SpineSprite3D::set_flip_h(bool flip) {
	if (flip_h == flip) {
		return;
	}
	flip_h = flip;
	visual_settings_changed();
}

bool SpineSprite3D::is_flipped_h() const {
	return flip_h;
}

void SpineSprite3D::set_flip_v(bool flip) {
	if (flip_v == flip) {
		return;
	}
	flip_v = flip;
	visual_settings_changed();
}

bool SpineSprite3D::is_flipped_v() const {
	return flip_v;
}

void SpineSprite3D::set_modulate(const Color &color) {
	if (modulate == color) {
		return;
	}
	modulate = color;
	visual_settings_changed();
}

Color SpineSprite3D::get_modulate() const {
	return modulate;
}

void SpineSprite3D::set_pixel_size(real_t size) {
	if (pixel_size == size) {
		return;
	}
	pixel_size = size;
	visual_settings_changed();
}

real_t SpineSprite3D::get_pixel_size() const {
	return pixel_size;
}

void SpineSprite3D::set_sprite_render_priority(int priority) {
	if (sprite_render_priority == priority) {
		return;
	}
	sprite_render_priority = priority;
	visual_settings_changed();
}

int SpineSprite3D::get_sprite_render_priority() const {
	return sprite_render_priority;
}

void SpineSprite3D::set_draw_flag(DrawFlags flag, bool enabled) {
	ERR_FAIL_INDEX(flag, FLAG_MAX);
	if (draw_flags[flag] == enabled) {
		return;
	}
	draw_flags[flag] = enabled;
	visual_settings_changed();
}

bool SpineSprite3D::get_draw_flag(DrawFlags flag) const {
	ERR_FAIL_INDEX_V(flag, FLAG_MAX, false);
	return draw_flags[flag];
}

void SpineSprite3D::set_billboard_mode(BaseMaterial3D::BillboardMode mode) {
	if (billboard_mode == mode) {
		return;
	}
	billboard_mode = mode;
	visual_settings_changed();
}

BaseMaterial3D::BillboardMode SpineSprite3D::get_billboard_mode() const {
	return billboard_mode;
}

void SpineSprite3D::set_texture_filter(BaseMaterial3D::TextureFilter filter) {
	if (texture_filter == filter) {
		return;
	}
	texture_filter = filter;
	visual_settings_changed();
}

BaseMaterial3D::TextureFilter SpineSprite3D::get_texture_filter() const {
	return texture_filter;
}

Ref<TriangleMesh> SpineSprite3D::generate_triangle_mesh() const {
	if (pick_triangle_mesh.is_valid()) {
		return pick_triangle_mesh;
	}

	Vector<Vector3> faces;
	for (int i = 0; i < mesh_instances.size(); i++) {
		const PackedVector3Array &vertices = mesh_instances[i]->pick_vertices;
		const PackedInt32Array &indices = mesh_instances[i]->pick_indices;
		if (vertices.is_empty() || indices.is_empty()) {
			continue;
		}

		for (int j = 0; j < indices.size(); j += 3) {
			if (j + 2 >= indices.size()) {
				break;
			}
			faces.push_back(vertices[indices[j]]);
			faces.push_back(vertices[indices[j + 1]]);
			faces.push_back(vertices[indices[j + 2]]);
		}
	}

	if (faces.is_empty()) {
		return Ref<TriangleMesh>();
	}

	pick_triangle_mesh.instantiate();
	pick_triangle_mesh->create(faces);
	return pick_triangle_mesh;
}

void SpineSprite3D::callback(spine::AnimationState *state, spine::EventType type, spine::TrackEntry *entry, spine::Event *event) {
	Ref<SpineTrackEntry> entry_ref = Ref<SpineTrackEntry>(memnew(SpineTrackEntry));
	entry_ref->set_spine_object(this, entry);
	Ref<SpineEvent> event_ref(nullptr);
	if (event) {
		event_ref = Ref<SpineEvent>(memnew(SpineEvent));
		event_ref->set_spine_object(this, event);
	}
	switch (type) {
		case spine::EventType_Start: emit_signal(SNAME("animation_started"), this, animation_state, entry_ref); break;
		case spine::EventType_Interrupt: emit_signal(SNAME("animation_interrupted"), this, animation_state, entry_ref); break;
		case spine::EventType_End: emit_signal(SNAME("animation_ended"), this, animation_state, entry_ref); break;
		case spine::EventType_Complete: emit_signal(SNAME("animation_completed"), this, animation_state, entry_ref); break;
		case spine::EventType_Dispose: emit_signal(SNAME("animation_disposed"), this, animation_state, entry_ref); break;
		case spine::EventType_Event: emit_signal(SNAME("animation_event"), this, animation_state, entry_ref, event_ref); break;
	}
}

Transform3D SpineSprite3D::get_global_bone_transform(const String &bone_name) {
	if (!skeleton.is_valid()) return get_global_transform();
	auto bone = skeleton->find_bone(bone_name);
	if (!bone.is_valid()) return get_global_transform();
	return get_global_transform() * spine_transform2d_to_local_3d(bone->get_global_transform());
}

void SpineSprite3D::set_global_bone_transform(const String &bone_name, const Transform3D &p_transform) {
	if (!skeleton.is_valid()) return;
	auto bone = skeleton->find_bone(bone_name);
	if (!bone.is_valid()) return;
	Transform3D local_3d = get_global_transform().affine_inverse() * p_transform;
	bone->set_global_transform(spine_transform3d_to_local_2d(local_3d));
}

SpineConstant::UpdateMode SpineSprite3D::get_update_mode() {
	return update_mode;
}

void SpineSprite3D::set_update_mode(SpineConstant::UpdateMode v) {
	update_mode = v;
	set_process_internal(update_mode == SpineConstant::UpdateMode_Process);
	set_physics_process_internal(update_mode == SpineConstant::UpdateMode_Physics);
}

Ref<SpineSkin> SpineSprite3D::new_skin(const String &name) {
	Ref<SpineSkin> skin = memnew(SpineSkin);
	skin->init(name, this);
	return skin;
}

Ref<Material> SpineSprite3D::get_normal_material() { return normal_material; }
void SpineSprite3D::set_normal_material(Ref<Material> p_material) { normal_material = p_material; }
Ref<Material> SpineSprite3D::get_additive_material() { return additive_material; }
void SpineSprite3D::set_additive_material(Ref<Material> p_material) { additive_material = p_material; }
Ref<Material> SpineSprite3D::get_multiply_material() { return multiply_material; }
void SpineSprite3D::set_multiply_material(Ref<Material> p_material) { multiply_material = p_material; }
Ref<Material> SpineSprite3D::get_screen_material() { return screen_material; }
void SpineSprite3D::set_screen_material(Ref<Material> p_material) { screen_material = p_material; }
void SpineSprite3D::set_time_scale(float p_time_scale) { time_scale = p_time_scale; }
float SpineSprite3D::get_time_scale() { return time_scale; }

void SpineSprite3D::debug_settings_changed() {
	if (skeleton.is_valid()) update_skeleton(0);
}

bool SpineSprite3D::get_debug_root() { return debug_root; }
void SpineSprite3D::set_debug_root(bool root) { debug_root = root; debug_settings_changed(); }
Color SpineSprite3D::get_debug_root_color() { return debug_root_color; }
void SpineSprite3D::set_debug_root_color(const Color &color) { debug_root_color = color; debug_settings_changed(); }
bool SpineSprite3D::get_debug_bones() { return debug_bones; }
void SpineSprite3D::set_debug_bones(bool bones) { debug_bones = bones; debug_settings_changed(); }
Color SpineSprite3D::get_debug_bones_color() { return debug_bones_color; }
void SpineSprite3D::set_debug_bones_color(const Color &color) { debug_bones_color = color; debug_settings_changed(); }
float SpineSprite3D::get_debug_bones_thickness() { return debug_bones_thickness; }
void SpineSprite3D::set_debug_bones_thickness(float thickness) { debug_bones_thickness = thickness; debug_settings_changed(); }
bool SpineSprite3D::get_debug_regions() { return debug_regions; }
void SpineSprite3D::set_debug_regions(bool regions) { debug_regions = regions; debug_settings_changed(); }
Color SpineSprite3D::get_debug_regions_color() { return debug_regions_color; }
void SpineSprite3D::set_debug_regions_color(const Color &color) { debug_regions_color = color; debug_settings_changed(); }
bool SpineSprite3D::get_debug_meshes() { return debug_meshes; }
void SpineSprite3D::set_debug_meshes(bool meshes) { debug_meshes = meshes; debug_settings_changed(); }
Color SpineSprite3D::get_debug_meshes_color() { return debug_meshes_color; }
void SpineSprite3D::set_debug_meshes_color(const Color &color) { debug_meshes_color = color; debug_settings_changed(); }
bool SpineSprite3D::get_debug_bounding_boxes() { return debug_bounding_boxes; }
void SpineSprite3D::set_debug_bounding_boxes(bool boxes) { debug_bounding_boxes = boxes; debug_settings_changed(); }
Color SpineSprite3D::get_debug_bounding_boxes_color() { return debug_bounding_boxes_color; }
void SpineSprite3D::set_debug_bounding_boxes_color(const Color &color) { debug_bounding_boxes_color = color; debug_settings_changed(); }
bool SpineSprite3D::get_debug_paths() { return debug_paths; }
void SpineSprite3D::set_debug_paths(bool paths) { debug_paths = paths; debug_settings_changed(); }
Color SpineSprite3D::get_debug_paths_color() { return debug_paths_color; }
void SpineSprite3D::set_debug_paths_color(const Color &color) { debug_paths_color = color; debug_settings_changed(); }
bool SpineSprite3D::get_debug_clipping() { return debug_clipping; }
void SpineSprite3D::set_debug_clipping(bool clipping) { debug_clipping = clipping; debug_settings_changed(); }
Color SpineSprite3D::get_debug_clipping_color() { return debug_clipping_color; }
void SpineSprite3D::set_debug_clipping_color(const Color &color) { debug_clipping_color = color; debug_settings_changed(); }

static void update_preview_animation(SpineSprite3D *sprite, const String &skin, const String &animation, bool frame, float time) {
	Engine *engine = Engine::get_singleton();
	if (!engine || !engine->is_editor_hint()) return;
	if (!sprite->is_inside_tree() || sprite->is_queued_for_deletion()) return;
	if (!sprite->get_skeleton().is_valid()) return;

	const String skin_name = spine_resolve_preview_skin(sprite->get_skeleton_data_res(), skin);
	const String animation_name = spine_normalize_preview_animation(animation);

	if (skin_name.is_empty()) {
		sprite->get_skeleton()->set_skin(nullptr);
	} else {
		sprite->get_skeleton()->set_skin_by_name(skin_name);
	}
	sprite->get_skeleton()->set_to_setup_pose();
	if (spine_preview_animation_is_none(animation_name)) {
		sprite->get_animation_state()->set_empty_animation(0, 0);
		sprite->update_skeleton(0);
		return;
	}

	auto track_entry = sprite->get_animation_state()->set_animation(animation_name, true, 0);
	track_entry->set_mix_duration(0);
	if (frame) {
		track_entry->set_time_scale(0);
		track_entry->set_track_time(time);
	}
	sprite->update_skeleton(0);
}

void SpineSprite3D::_get_property_list(List<PropertyInfo> *list) const {
	if (!skeleton_data_res.is_valid() || !skeleton_data_res->is_skeleton_data_loaded()) return;
#ifdef SPINE_GODOT_EXTENSION
	PackedStringArray animation_names;
	PackedStringArray skin_names;
#else
	Vector<String> animation_names;
	Vector<String> skin_names;
#endif
	skeleton_data_res->get_animation_names(animation_names);
	skeleton_data_res->get_skin_names(skin_names);
	animation_names.insert(0, SPINE_PREVIEW_NONE);

	PropertyInfo preview_skin_property;
	preview_skin_property.name = "preview_skin";
	preview_skin_property.type = Variant::STRING;
	preview_skin_property.usage = PROPERTY_USAGE_EDITOR | PROPERTY_USAGE_STORAGE;
	preview_skin_property.hint_string = String(",").join(skin_names);
	preview_skin_property.hint = PROPERTY_HINT_ENUM;
	list->push_back(preview_skin_property);

	PropertyInfo preview_anim_property;
	preview_anim_property.name = "preview_animation";
	preview_anim_property.type = Variant::STRING;
	preview_anim_property.usage = PROPERTY_USAGE_EDITOR | PROPERTY_USAGE_STORAGE;
	preview_anim_property.hint_string = String(",").join(animation_names);
	preview_anim_property.hint = PROPERTY_HINT_ENUM;
	list->push_back(preview_anim_property);

	PropertyInfo preview_frame_property;
	preview_frame_property.name = "preview_frame";
	preview_frame_property.type = Variant::BOOL;
	preview_frame_property.usage = PROPERTY_USAGE_EDITOR | PROPERTY_USAGE_STORAGE;
	list->push_back(preview_frame_property);

	PropertyInfo preview_time_property;
	preview_time_property.name = "preview_time";
	preview_time_property.type = VARIANT_FLOAT;
	preview_time_property.usage = PROPERTY_USAGE_EDITOR | PROPERTY_USAGE_STORAGE;
	float animation_duration = 0;
	if (!spine_preview_animation_is_none(preview_animation)) {
		auto animation = skeleton_data_res->find_animation(preview_animation);
		if (animation.is_valid()) animation_duration = animation->get_duration();
	}
#ifdef SPINE_GODOT_EXTENSION
	preview_time_property.hint_string = String("0.0,") + String::num(animation_duration) + String(",0.01");
#else
	preview_time_property.hint_string = String("0.0,{0},0.01").format(varray(animation_duration));
#endif
	preview_time_property.hint = PROPERTY_HINT_RANGE;
	list->push_back(preview_time_property);
}

bool SpineSprite3D::_get(const StringName &p_property, Variant &value) const {
	if (p_property == StringName("preview_skin")) {
		value = spine_resolve_preview_skin(skeleton_data_res, preview_skin);
		return true;
	}
	if (p_property == StringName("preview_animation")) {
		value = spine_normalize_preview_animation(preview_animation);
		return true;
	}
	if (p_property == StringName("preview_frame")) {
		value = preview_frame;
		return true;
	}
	if (p_property == StringName("preview_time")) {
		value = preview_time;
		return true;
	}
	return false;
}

bool SpineSprite3D::_set(const StringName &p_property, const Variant &value) {
	if (p_property == StringName("preview_skin")) {
		preview_skin = spine_resolve_preview_skin(skeleton_data_res, value);
		update_preview_animation(this, preview_skin, preview_animation, preview_frame, preview_time);
		NOTIFY_PROPERTY_LIST_CHANGED();
		return true;
	}
	if (p_property == StringName("preview_animation")) {
		preview_animation = spine_normalize_preview_animation(value);
		update_preview_animation(this, preview_skin, preview_animation, preview_frame, preview_time);
		NOTIFY_PROPERTY_LIST_CHANGED();
		return true;
	}
	if (p_property == StringName("preview_frame")) {
		preview_frame = value;
		update_preview_animation(this, preview_skin, preview_animation, preview_frame, preview_time);
		return true;
	}
	if (p_property == StringName("preview_time")) {
		preview_time = value;
		update_preview_animation(this, preview_skin, preview_animation, preview_frame, preview_time);
		return true;
	}
	return false;
}

static void add_debug_line(Ref<ImmediateMesh> &mesh, const Vector3 &from, const Vector3 &to, const Color &color) {
	mesh->surface_set_color(color);
	mesh->surface_add_vertex(from);
	mesh->surface_add_vertex(to);
}

static void add_debug_polyline(Ref<ImmediateMesh> &mesh, const PackedVector3Array &points, const Color &color, bool close_loop) {
	for (int i = 0; i < points.size() - 1; i++) {
		add_debug_line(mesh, points[i], points[i + 1], color);
	}
	if (close_loop && points.size() > 1) {
		add_debug_line(mesh, points[points.size() - 1], points[0], color);
	}
}

static void add_debug_triangle_lines(const SpineSprite3D *sprite, Ref<ImmediateMesh> &mesh, spine::Array<unsigned short> &triangles,
									 spine::Array<float> *vertices, const Color &color, int draw_order) {
	for (int t = 0; t < (int)triangles.size(); t += 3) {
		Vector3 v1 = sprite->spine_vertex_to_local(vertices->buffer()[triangles[t] * 2], vertices->buffer()[triangles[t] * 2 + 1], draw_order);
		Vector3 v2 = sprite->spine_vertex_to_local(vertices->buffer()[triangles[t + 1] * 2], vertices->buffer()[triangles[t + 1] * 2 + 1], draw_order);
		Vector3 v3 = sprite->spine_vertex_to_local(vertices->buffer()[triangles[t + 2] * 2], vertices->buffer()[triangles[t + 2] * 2 + 1], draw_order);
		add_debug_line(mesh, v1, v2, color);
		add_debug_line(mesh, v2, v3, color);
		add_debug_line(mesh, v3, v1, color);
	}
}

void SpineSprite3D::ensure_debug_mesh_instance() {
	if (debug_mesh_instance) return;
	debug_mesh_instance = memnew(MeshInstance3D);
	debug_mesh_instance->set_name("SpineDebugDraw");
	debug_mesh_instance->set_sorting_offset(1000.0f);
	debug_mesh_instance->set_sorting_use_aabb_center(false);
	Ref<StandardMaterial3D> debug_material(memnew(StandardMaterial3D));
	debug_material->set_shading_mode(BaseMaterial3D::SHADING_MODE_UNSHADED);
	debug_material->set_flag(BaseMaterial3D::FLAG_ALBEDO_FROM_VERTEX_COLOR, true);
	debug_material->set_transparency(BaseMaterial3D::TRANSPARENCY_ALPHA);
	debug_material->set_depth_draw_mode(BaseMaterial3D::DEPTH_DRAW_DISABLED);
	debug_material->set_render_priority(127);
	debug_mesh_instance->set_material_override(debug_material);
	add_child(debug_mesh_instance, false, INTERNAL_MODE_BACK);
	debug_mesh_instance->set_owner(this);
#ifdef TOOLS_ENABLED
	debug_mesh_instance->set_meta("_edit_lock_", true);
#endif
	move_child(debug_mesh_instance, -1);
}

void SpineSprite3D::draw_debug_bone(Ref<ImmediateMesh> &mesh, spine::Bone *bone, const Color &color) {
	float bone_length = bone->getData().getLength();
	if (bone_length == 0) bone_length = debug_bones_thickness * 2;
	float z = 0.05f;
	Transform3D bone_transform(Basis(Vector3(0, 0, 1), spine::MathUtil::Deg_Rad * bone->getAppliedPose().getWorldRotationX()),
							   Vector3(bone->getAppliedPose().getWorldX(), -bone->getAppliedPose().getWorldY(), z));
	bone_transform.basis = bone_transform.basis.scaled(Vector3(bone->getAppliedPose().getWorldScaleX(), bone->getAppliedPose().getWorldScaleY(), 1));
	Vector3 p0 = bone_transform.xform(Vector3(-debug_bones_thickness, 0, 0));
	Vector3 p1 = bone_transform.xform(Vector3(0, debug_bones_thickness, 0));
	Vector3 p2 = bone_transform.xform(Vector3(bone_length, 0, 0));
	Vector3 p3 = bone_transform.xform(Vector3(0, -debug_bones_thickness, 0));
	add_debug_line(mesh, p0, p1, color);
	add_debug_line(mesh, p1, p2, color);
	add_debug_line(mesh, p2, p3, color);
	add_debug_line(mesh, p3, p0, color);
}

void SpineSprite3D::draw_debug() {
	bool any_debug = debug_root || debug_bones || debug_regions || debug_meshes || debug_bounding_boxes || debug_clipping;
	if (!any_debug || !is_renderable() || !skeleton.is_valid() || !skeleton->get_spine_object()) {
		if (debug_mesh_instance) debug_mesh_instance->set_visible(false);
		return;
	}
	if (!Engine::get_singleton()->is_editor_hint() && (!get_tree() || !get_tree()->is_debugging_collisions_hint())) {
		if (debug_mesh_instance) debug_mesh_instance->set_visible(false);
		return;
	}

	ensure_debug_mesh_instance();
	debug_mesh_instance->set_visible(true);
	Ref<ImmediateMesh> mesh = Ref<ImmediateMesh>(memnew(ImmediateMesh));
	mesh->surface_begin(Mesh::PRIMITIVE_LINES);

	auto &statics = SpineSprite3DStatics::instance();
	auto &draw_order = skeleton->get_spine_object()->getDrawOrder().getAppliedPose();

	if (debug_regions) {
		for (int i = 0; i < (int)draw_order.size(); i++) {
			auto slot = draw_order[i];
			if (!slot->getBone().isActive()) continue;
			auto attachment = slot->getAppliedPose().getAttachment();
			if (!attachment || !attachment->getRTTI().isExactly(spine::RegionAttachment::rtti)) continue;
			auto region = (spine::RegionAttachment *)attachment;
			auto &sequence = region->getSequence();
			int sequence_index = sequence.resolveIndex(slot->getAppliedPose());
			auto vertices = &statics.scratch_vertices;
			vertices->setSize(8, 0);
			region->computeWorldVertices(*slot, sequence.getOffsets(sequence_index).buffer(), vertices->buffer(), 0);
			add_debug_triangle_lines(this, mesh, statics.quad_indices, vertices, debug_regions_color, i);
			PackedVector3Array hull;
			hull.resize(4);
			for (int j = 0; j < 4; j++) {
				hull.set(j, spine_vertex_to_local(vertices->buffer()[j * 2], vertices->buffer()[j * 2 + 1], i));
			}
			add_debug_polyline(mesh, hull, debug_regions_color, true);
		}
	}

	if (debug_meshes) {
		for (int i = 0; i < (int)draw_order.size(); i++) {
			auto slot = draw_order[i];
			if (!slot->getBone().isActive()) continue;
			auto attachment = slot->getAppliedPose().getAttachment();
			if (!attachment || !attachment->getRTTI().isExactly(spine::MeshAttachment::rtti)) continue;
			auto mesh_attachment = (spine::MeshAttachment *)attachment;
			auto vertices = &statics.scratch_vertices;
			vertices->setSize(mesh_attachment->getWorldVerticesLength(), 0);
			mesh_attachment->computeWorldVertices(*skeleton->get_spine_object(), *slot, 0, mesh_attachment->getWorldVerticesLength(), vertices->buffer(), 0, 2);
			add_debug_triangle_lines(this, mesh, mesh_attachment->getTriangles(), vertices, debug_meshes_color, i);
			PackedVector3Array hull;
			hull.resize(mesh_attachment->getHullLength() / 2);
			for (int j = 0, k = 0; j < mesh_attachment->getHullLength(); j += 2, k++) {
				hull.set(k, spine_vertex_to_local(vertices->buffer()[j], vertices->buffer()[j + 1], i));
			}
			add_debug_polyline(mesh, hull, debug_meshes_color, true);
		}
	}

	if (debug_bounding_boxes) {
		for (int i = 0; i < (int)draw_order.size(); i++) {
			auto slot = draw_order[i];
			if (!slot->getBone().isActive()) continue;
			auto attachment = slot->getAppliedPose().getAttachment();
			if (!attachment || !attachment->getRTTI().isExactly(spine::BoundingBoxAttachment::rtti)) continue;
			auto bounding_box = (spine::BoundingBoxAttachment *)attachment;
			auto vertices = &statics.scratch_vertices;
			vertices->setSize(bounding_box->getWorldVerticesLength(), 0);
			bounding_box->computeWorldVertices(*skeleton->get_spine_object(), *slot, 0, bounding_box->getWorldVerticesLength(), vertices->buffer(), 0, 2);
			PackedVector3Array points;
			points.resize((int)vertices->size() / 2);
			for (int j = 0; j < points.size(); j++) {
				points.set(j, spine_vertex_to_local(vertices->buffer()[j * 2], vertices->buffer()[j * 2 + 1], i));
			}
			add_debug_polyline(mesh, points, debug_bounding_boxes_color, true);
		}
	}

	if (debug_clipping) {
		for (int i = 0; i < (int)draw_order.size(); i++) {
			auto slot = draw_order[i];
			if (!slot->getBone().isActive()) continue;
			auto attachment = slot->getAppliedPose().getAttachment();
			if (!attachment || !attachment->getRTTI().isExactly(spine::ClippingAttachment::rtti)) continue;
			auto clipping = (spine::ClippingAttachment *)attachment;
			auto vertices = &statics.scratch_vertices;
			vertices->setSize(clipping->getWorldVerticesLength(), 0);
			clipping->computeWorldVertices(*skeleton->get_spine_object(), *slot, 0, clipping->getWorldVerticesLength(), vertices->buffer(), 0, 2);
			PackedVector3Array points;
			points.resize((int)vertices->size() / 2);
			for (int j = 0; j < points.size(); j++) {
				points.set(j, spine_vertex_to_local(vertices->buffer()[j * 2], vertices->buffer()[j * 2 + 1], i));
			}
			add_debug_polyline(mesh, points, debug_clipping_color, true);
		}
	}

	if (debug_root) {
		draw_debug_bone(mesh, skeleton->get_spine_object()->getRootBone(), debug_root_color);
	}
	if (debug_bones) {
		auto &bones = skeleton->get_spine_object()->getBones();
		for (int i = 0; i < (int)bones.size(); i++) {
			if (!bones[i]->isActive()) continue;
			draw_debug_bone(mesh, bones[i], debug_bones_color);
		}
	}

	mesh->surface_end();
	debug_mesh_instance->set_mesh(mesh);
}

#endif
