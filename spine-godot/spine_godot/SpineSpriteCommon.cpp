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

#include "SpineSpriteCommon.h"
#include "SpineSprite2D.h"
#if VERSION_MAJOR > 3
#include "SpineSprite3D.h"
#endif
#include "SpineSkeleton.h"

Ref<SpineSkeletonDataResource> spine_sprite_get_skeleton_data_res(Object *owner) {
	if (!owner) return Ref<SpineSkeletonDataResource>();
	if (SpineSprite2D *sprite = Object::cast_to<SpineSprite2D>(owner)) return sprite->get_skeleton_data_res();
#if VERSION_MAJOR > 3
	if (SpineSprite3D *sprite = Object::cast_to<SpineSprite3D>(owner)) return sprite->get_skeleton_data_res();
#endif
	return Ref<SpineSkeletonDataResource>();
}

bool spine_sprite_is_visible_in_tree(Object *owner) {
	if (!owner) return false;
	if (SpineSprite2D *sprite = Object::cast_to<SpineSprite2D>(owner)) return sprite->is_visible_in_tree();
#if VERSION_MAJOR > 3
	if (SpineSprite3D *sprite = Object::cast_to<SpineSprite3D>(owner)) return sprite->is_visible_in_tree();
#endif
	return false;
}

void spine_sprite_set_modified_bones(Object *owner) {
	if (!owner) return;
	if (SpineSprite2D *sprite = Object::cast_to<SpineSprite2D>(owner)) sprite->set_modified_bones();
#if VERSION_MAJOR > 3
	else if (SpineSprite3D *sprite = Object::cast_to<SpineSprite3D>(owner)) sprite->set_modified_bones();
#endif
}

Transform2D spine_sprite_get_global_transform_2d(Object *owner) {
	if (!owner) return Transform2D();
	if (SpineSprite2D *sprite = Object::cast_to<SpineSprite2D>(owner)) return sprite->get_global_transform();
#if VERSION_MAJOR > 3
	if (SpineSprite3D *sprite = Object::cast_to<SpineSprite3D>(owner)) return spine_transform3d_to_local_2d(sprite->get_global_transform());
#endif
	return Transform2D();
}

Transform2D spine_sprite_get_global_transform_2d_affine_inverse(Object *owner) {
	return spine_sprite_get_global_transform_2d(owner).affine_inverse();
}

Ref<SpineSkeleton> spine_sprite_get_skeleton_ref(Object *owner) {
	if (!owner) return Ref<SpineSkeleton>();
	if (SpineSprite2D *sprite = Object::cast_to<SpineSprite2D>(owner)) return sprite->get_skeleton();
#if VERSION_MAJOR > 3
	if (SpineSprite3D *sprite = Object::cast_to<SpineSprite3D>(owner)) return sprite->get_skeleton();
#endif
	return Ref<SpineSkeleton>();
}

Transform3D spine_transform2d_to_local_3d(const Transform2D &t) {
	Basis basis(Vector3(t[0].x, -t[0].y, 0), Vector3(t[1].x, -t[1].y, 0), Vector3(0, 0, 1));
	return Transform3D(basis, Vector3(t[2].x, -t[2].y, 0));
}

Transform2D spine_transform3d_to_local_2d(const Transform3D &t) {
	return Transform2D(Vector2(t.basis.rows[0].x, -t.basis.rows[0].y), Vector2(t.basis.rows[1].x, -t.basis.rows[1].y),
					   Vector2(t.origin.x, -t.origin.y));
}
