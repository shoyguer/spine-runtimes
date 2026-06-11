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

#pragma once

#include "SpineCommon.h"

#if VERSION_MAJOR > 3

#include "SpineSkeleton.h"
#include "SpineAnimationState.h"
#ifdef SPINE_GODOT_EXTENSION
#include <godot_cpp/classes/node3d.hpp>
#include <godot_cpp/classes/mesh_instance3d.hpp>
#include <godot_cpp/classes/array_mesh.hpp>
#include <godot_cpp/classes/immediate_mesh.hpp>
#include <godot_cpp/classes/standard_material3d.hpp>
#else
#include "scene/3d/node_3d.h"
#include "scene/3d/mesh_instance_3d.h"
#include "scene/resources/material.h"
#include "scene/resources/mesh.h"
#include "scene/resources/immediate_mesh.h"
#endif

struct SpineRendererObject;

class SpineSprite3D;

class SpineMesh3D : public MeshInstance3D {
	GDCLASS(SpineMesh3D, MeshInstance3D)

	friend class SpineSprite3D;

protected:
	static void _bind_methods();

	Ref<ArrayMesh> array_mesh;
	Ref<Material> slot_material;
	Ref<Material> cached_slot_material;
	SpineRendererObject *renderer_object;
	SpineRendererObject *cached_renderer_object;
	Ref<Texture2D> cached_albedo_texture;
	Ref<StandardMaterial3D> cached_surface_material;
	bool mesh_dirty;
	bool surface_material_dirty;
	bool mesh_assigned;
	bool instance_refresh_needed;
	int last_vertex_count;
	int last_index_count;
	int cached_draw_order;
#if VERSION_MAJOR > 3
	PackedByteArray vertex_buffer;
	PackedByteArray attribute_buffer;
	uint32_t surface_offsets[Mesh::ARRAY_MAX];
	uint32_t vertex_stride;
	uint32_t attribute_stride;
#endif
	PackedVector3Array pick_vertices;
	PackedInt32Array pick_indices;

	Ref<StandardMaterial3D> build_surface_material(const Ref<Texture2D> &p_albedo_texture, int draw_order);
	void sync_mesh_aabb(const PackedVector3Array &p_vertices);
	void apply_surface_material(bool p_force_instance_refresh = false);
	void clear_mesh_surface();
	void rebuild_mesh_surface(const PackedVector3Array &p_vertices, const PackedVector2Array &p_uvs, const PackedColorArray &p_colors,
							  const PackedInt32Array &p_indices);
	void update_mesh_surface_buffers(const PackedVector3Array &p_vertices, const PackedVector2Array &p_uvs, const PackedColorArray &p_colors);

public:
	SpineMesh3D();
	void set_material(const Ref<Material> &p_material);
	void update_mesh(const PackedVector3Array &p_vertices, const PackedVector2Array &p_uvs, const PackedColorArray &p_colors,
					 const PackedInt32Array &p_indices, const Ref<Texture2D> &p_albedo_texture, int draw_order);
};

class SpineSprite3D : public Node3D, public spine::AnimationStateListenerObject {
	GDCLASS(SpineSprite3D, Node3D)

	friend class SpineBone;
	friend class SpineMesh3D;
	friend class SpineSprite3DGizmoPlugin;

public:
	enum DrawFlags {
		FLAG_TRANSPARENT,
		FLAG_SHADED,
		FLAG_DOUBLE_SIDED,
		FLAG_DISABLE_DEPTH_TEST,
		FLAG_FIXED_SIZE,
		FLAG_MAX
	};

protected:
	Ref<SpineSkeletonDataResource> skeleton_data_res;
	Ref<SpineSkeleton> skeleton;
	Ref<SpineAnimationState> animation_state;
	SpineConstant::UpdateMode update_mode;
	float time_scale;

	String preview_skin;
	String preview_animation;
	bool preview_frame;
	float preview_time;

	bool debug_root;
	Color debug_root_color;
	bool debug_bones;
	Color debug_bones_color;
	float debug_bones_thickness;
	bool debug_regions;
	Color debug_regions_color;
	bool debug_meshes;
	Color debug_meshes_color;
	bool debug_bounding_boxes;
	Color debug_bounding_boxes_color;
	bool debug_paths;
	Color debug_paths_color;
	bool debug_clipping;
	Color debug_clipping_color;

	Vector<SpineMesh3D *> mesh_instances;
	PackedVector3Array scratch_mesh_vertices;
	PackedVector2Array scratch_mesh_uvs;
	PackedColorArray scratch_mesh_colors;
	PackedInt32Array scratch_mesh_indices;
	bool atlas_textures_pending_refresh;
	MeshInstance3D *debug_mesh_instance;
	Ref<Material> normal_material;
	Ref<Material> additive_material;
	Ref<Material> multiply_material;
	Ref<Material> screen_material;
	spine::SkeletonClipping *skeleton_clipper;
	bool modified_bones;

	bool flip_h;
	bool flip_v;
	float flip_origin_x;
	float flip_origin_y;
	bool flip_origin_valid;
	Color modulate;
	real_t pixel_size;
	int sprite_render_priority;
	bool draw_flags[FLAG_MAX];
	BaseMaterial3D::BillboardMode billboard_mode;
	BaseMaterial3D::TextureFilter texture_filter;
	mutable Ref<TriangleMesh> pick_triangle_mesh;

	static void _bind_methods();
	void visual_settings_changed();
	void connect_atlas_texture_refresh();
	void refresh_atlas_page_textures();
	void schedule_display_refresh();
	Ref<Texture2D> resolve_albedo_texture(SpineRendererObject *p_renderer_object);
	void bind_editor_import_refresh();
	void on_editor_filesystem_changed();
	void update_flip_origin(spine::Skeleton *skeleton_obj);
	void configure_slot_material(StandardMaterial3D *p_material, int draw_order) const;
	void _notification(int what);
	void _get_property_list(List<PropertyInfo> *list) const;
	bool _get(const StringName &p_property, Variant &value) const;
	bool _set(const StringName &p_property, const Variant &value);

	void generate_meshes_for_slots(Ref<SpineSkeleton> skeleton_ref);
	void remove_meshes();
	void update_meshes(Ref<SpineSkeleton> skeleton_ref);
	void draw_debug();
	void draw_debug_bone(Ref<ImmediateMesh> &mesh, spine::Bone *bone, const Color &color);
	void ensure_debug_mesh_instance();
	void debug_settings_changed();
	void callback(spine::AnimationState *state, spine::EventType type, spine::TrackEntry *entry, spine::Event *event) override;

public:
	SpineSprite3D();
	~SpineSprite3D();

	void set_skeleton_data_res(const Ref<SpineSkeletonDataResource> &resource);
	Ref<SpineSkeletonDataResource> get_skeleton_data_res();
	Ref<SpineSkeleton> get_skeleton();
	Ref<SpineAnimationState> get_animation_state();

	void on_skeleton_data_changed();
	void set_modified_bones() {
		modified_bones = true;
	}
	void update_skeleton(float delta);

	Transform3D get_global_bone_transform(const String &bone_name);
	void set_global_bone_transform(const String &bone_name, const Transform3D &p_transform);

	SpineConstant::UpdateMode get_update_mode();
	void set_update_mode(SpineConstant::UpdateMode v);

	Ref<SpineSkin> new_skin(const String &name);

	Ref<Material> get_normal_material();
	void set_normal_material(Ref<Material> p_material);
	Ref<Material> get_additive_material();
	void set_additive_material(Ref<Material> p_material);
	Ref<Material> get_multiply_material();
	void set_multiply_material(Ref<Material> p_material);
	Ref<Material> get_screen_material();
	void set_screen_material(Ref<Material> p_material);

	void set_time_scale(float p_time_scale);
	float get_time_scale();

	bool get_debug_root();
	void set_debug_root(bool root);
	Color get_debug_root_color();
	void set_debug_root_color(const Color &color);
	bool get_debug_bones();
	void set_debug_bones(bool bones);
	Color get_debug_bones_color();
	void set_debug_bones_color(const Color &color);
	float get_debug_bones_thickness();
	void set_debug_bones_thickness(float thickness);
	bool get_debug_regions();
	void set_debug_regions(bool regions);
	Color get_debug_regions_color();
	void set_debug_regions_color(const Color &color);
	bool get_debug_meshes();
	void set_debug_meshes(bool meshes);
	Color get_debug_meshes_color();
	void set_debug_meshes_color(const Color &color);
	bool get_debug_bounding_boxes();
	void set_debug_bounding_boxes(bool boxes);
	Color get_debug_bounding_boxes_color();
	void set_debug_bounding_boxes_color(const Color &color);
	bool get_debug_paths();
	void set_debug_paths(bool paths);
	Color get_debug_paths_color();
	void set_debug_paths_color(const Color &color);
	bool get_debug_clipping();
	void set_debug_clipping(bool clipping);
	Color get_debug_clipping_color();
	void set_debug_clipping_color(const Color &color);

	void set_flip_h(bool flip);
	bool is_flipped_h() const;
	void set_flip_v(bool flip);
	bool is_flipped_v() const;
	void set_modulate(const Color &color);
	Color get_modulate() const;
	void set_pixel_size(real_t size);
	real_t get_pixel_size() const;
	void set_sprite_render_priority(int priority);
	int get_sprite_render_priority() const;
	void set_draw_flag(DrawFlags flag, bool enabled);
	bool get_draw_flag(DrawFlags flag) const;
	void set_billboard_mode(BaseMaterial3D::BillboardMode mode);
	BaseMaterial3D::BillboardMode get_billboard_mode() const;
	void set_texture_filter(BaseMaterial3D::TextureFilter filter);
	BaseMaterial3D::TextureFilter get_texture_filter() const;

	Ref<TriangleMesh> generate_triangle_mesh() const;
	Vector3 spine_vertex_to_local(float x, float y, int draw_order) const;
	void refresh_display();

	static void clear_statics();
};

VARIANT_ENUM_CAST(SpineSprite3D::DrawFlags);

#endif
