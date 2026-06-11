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

#if VERSION_MAJOR > 3 && defined(TOOLS_ENABLED) && !defined(SPINE_GODOT_EXTENSION)

#include "SpineSprite3DGizmoPlugin.h"
#include "SpineSprite3D.h"

bool SpineSprite3DGizmoPlugin::has_gizmo(Node3D *p_spatial) {
	return Object::cast_to<SpineSprite3D>(p_spatial) != nullptr;
}

String SpineSprite3DGizmoPlugin::get_gizmo_name() const {
	return "SpineSprite3D";
}

int SpineSprite3DGizmoPlugin::get_priority() const {
	return -1;
}

bool SpineSprite3DGizmoPlugin::can_be_hidden() const {
	return false;
}

void SpineSprite3DGizmoPlugin::redraw(EditorNode3DGizmo *p_gizmo) {
	SpineSprite3D *sprite = Object::cast_to<SpineSprite3D>(p_gizmo->get_node_3d());
	p_gizmo->clear();

	Ref<TriangleMesh> triangle_mesh = sprite->generate_triangle_mesh();
	if (triangle_mesh.is_valid()) {
		p_gizmo->add_collision_triangles(triangle_mesh);
	}
}

#endif
