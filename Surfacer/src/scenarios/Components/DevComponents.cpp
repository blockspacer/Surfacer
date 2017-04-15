//
//  DevComponents.cpp
//  Surfacer
//
//  Created by Shamyl Zakariya on 4/14/17.
//
//

#include "DevComponents.hpp"

void WorldCoordinateSystemDrawComponent::draw(const core::render_state &state) {
	const cpBB frustum = state.viewport->getFrustum();
	const int gridSize = _gridSize;
	const int majorGridSize = _gridSize * 10;
	const int firstY = static_cast<int>(floor(frustum.b / gridSize) * gridSize);
	const int lastY = static_cast<int>(floor(frustum.t / gridSize) * gridSize) + gridSize;
	const int firstX = static_cast<int>(floor(frustum.l / gridSize) * gridSize);
	const int lastX = static_cast<int>(floor(frustum.r / gridSize) * gridSize) + gridSize;

	const auto MinorLineColor = ColorA(1,1,1,0.05);
	const auto MajorLineColor = ColorA(1,1,1,0.25);
	const auto AxisColor = ColorA(1,0,0,1);
	gl::lineWidth(1.0 / state.viewport->getScale());

	for (int y = firstY; y <= lastY; y+= gridSize) {
		if (y == 0) {
			gl::color(AxisColor);
		} else if ( y % majorGridSize == 0) {
			gl::color(MajorLineColor);
		} else {
			gl::color(MinorLineColor);
		}
		gl::drawLine(vec2(firstX, y), vec2(lastX, y));
	}

	for (int x = firstX; x <= lastX; x+= gridSize) {
		if (x == 0) {
			gl::color(AxisColor);
		} else if ( x % majorGridSize == 0) {
			gl::color(MajorLineColor);
		} else {
			gl::color(MinorLineColor);
		}
		gl::drawLine(vec2(x, firstY), vec2(x, lastY));
	}
}

#pragma mark - CameraControlComponent

CameraControlComponent::CameraControlComponent(core::Camera2DInterfaceRef viewport, int dispatchReceiptIndex):
InputComponent(dispatchReceiptIndex),
_viewport(viewport)
{}

bool CameraControlComponent::mouseDown( const ci::app::MouseEvent &event ) {
	_mouseScreen = event.getPos();
	_mouseWorld = getLevel()->getViewport()->screenToWorld(_mouseScreen);

	// capture alt key for re-centering
	if ( event.isAltDown() )
	{
		_viewport->lookAt( _mouseWorld );
		return true;
	}

	// capture space key for dragging
	if ( isKeyDown( app::KeyEvent::KEY_SPACE )) {
		return true;
	}

	return false;
}

bool CameraControlComponent::mouseUp( const ci::app::MouseEvent &event ) {
	return false;
}

bool CameraControlComponent::mouseMove( const ci::app::MouseEvent &event, const ivec2 &delta ) {
	_mouseScreen = event.getPos();
	_mouseWorld = getLevel()->getViewport()->screenToWorld(_mouseScreen);
	return false;
}

bool CameraControlComponent::mouseDrag( const ci::app::MouseEvent &event, const ivec2 &delta ) {
	_mouseScreen = event.getPos();
	_mouseWorld = getLevel()->getViewport()->screenToWorld(_mouseScreen);

	if ( isKeyDown( app::KeyEvent::KEY_SPACE ))
	{
		_viewport->setPan( _viewport->getPan() + dvec2(delta) );
		return true;
	}

	return false;
}

bool CameraControlComponent::mouseWheel( const ci::app::MouseEvent &event ){
	const float zoom = _viewport->getScale(),
	wheelScale = 0.1 * zoom,
	dz = (event.getWheelIncrement() * wheelScale);
	_viewport->setScale( std::max( zoom + dz, 0.1f ), event.getPos() );

	return true;
}

#pragma mark - MousePickComponent

MousePickComponent::MousePickComponent(cpShapeFilter pickFilter, int dispatchReceiptIndex):
InputComponent(dispatchReceiptIndex),
_pickFilter(pickFilter),
_mouseBody(nullptr),
_draggingBody(nullptr),
_mouseJoint(nullptr)
{}

MousePickComponent::~MousePickComponent() {
	releaseDragConstraint();
	cpBodyFree(_mouseBody);
}

void MousePickComponent::onReady(core::GameObjectRef parent, core::LevelRef level) {
	InputComponent::onReady(parent, level);
	_mouseBody = cpBodyNewKinematic();
}

void MousePickComponent::step(const core::time_state &time){
	cpVect mouseBodyPos = cpv(_mouseWorld);
	cpVect newMouseBodyPos = cpvlerp(cpBodyGetPosition(_mouseBody), mouseBodyPos, 0.75);

	cpBodySetVelocity(_mouseBody, cpvmult(cpvsub(newMouseBodyPos, cpBodyGetPosition(_mouseBody)), time.deltaT));
	cpBodySetPosition(_mouseBody, newMouseBodyPos);
}

bool MousePickComponent::mouseDown( const ci::app::MouseEvent &event ) {
	releaseDragConstraint();

	_mouseScreen = event.getPos();
	_mouseWorld = getLevel()->getViewport()->screenToWorld(_mouseScreen);

	if ( isKeyDown( app::KeyEvent::KEY_SPACE )) {
		return false;
	}

	const float distance = 1.f;
	cpPointQueryInfo info = {};
	cpShape *pick = cpSpacePointQueryNearest(getLevel()->getSpace(), cpv(_mouseWorld), distance, _pickFilter, &info);
	if (pick) {
		cpBody *pickBody = cpShapeGetBody(pick);

		if (pickBody && cpBodyGetType(pickBody) == CP_BODY_TYPE_DYNAMIC) {
			cpVect nearest = (info.distance > 0.0f ? info.point : cpv(_mouseWorld));

			_draggingBody = pickBody;
			_mouseJoint = cpPivotJointNew2(_mouseBody, _draggingBody, cpvzero, cpBodyWorldToLocal(_draggingBody,nearest));

			cpSpaceAddConstraint(getLevel()->getSpace(), _mouseJoint);

			return true;
		}
	}

	return false;
}

bool MousePickComponent::mouseUp( const ci::app::MouseEvent &event ) {
	releaseDragConstraint();
	return false;
}

bool MousePickComponent::mouseMove( const ci::app::MouseEvent &event, const ivec2 &delta ) {
	_mouseScreen = event.getPos();
	_mouseWorld = getLevel()->getViewport()->screenToWorld(_mouseScreen);
	return false;
}

bool MousePickComponent::mouseDrag( const ci::app::MouseEvent &event, const ivec2 &delta ){
	_mouseScreen = event.getPos();
	_mouseWorld = getLevel()->getViewport()->screenToWorld(_mouseScreen);
	return false;
}

void MousePickComponent::releaseDragConstraint() {
	if (_mouseJoint) {
		cpSpaceRemoveConstraint(getLevel()->getSpace(), _mouseJoint);
		cpConstraintFree(_mouseJoint);
		_mouseJoint = nullptr;
		_draggingBody = nullptr;
	}
}

#pragma mark - MousePickDrawComponent

MousePickDrawComponent::MousePickDrawComponent(ColorA color, float radius):
_color(color),
_radius(radius)
{}

void MousePickDrawComponent::onReady(core::GameObjectRef parent, core::LevelRef level) {
	DrawComponent::onReady(parent,level);
	_pickComponent = getSibling<MousePickComponent>();
}

void MousePickDrawComponent::draw(const core::render_state &renderState) {
	if (shared_ptr<MousePickComponent> pc = _pickComponent.lock()) {
		if (pc->isDragging()) {
			const float radius = _radius * renderState.viewport->getReciprocalScale();
			gl::color(_color);
			gl::drawSolidCircle(pc->getDragPositionWorld(), radius);
		}
	}
}

#pragma mark - MouseCutterComponent

MouseCutterComponent::MouseCutterComponent(terrain::TerrainObjectRef terrain, cpShapeFilter cutFilter, float radius, int dispatchReceiptIndex):
InputComponent(dispatchReceiptIndex),
_cutting(false),
_radius(radius),
_cutFilter(cutFilter),
_terrain(terrain)
{}

bool MouseCutterComponent::mouseDown( const ci::app::MouseEvent &event ) {
	_mouseScreen = event.getPos();
	_mouseWorld = getLevel()->getViewport()->screenToWorld(_mouseScreen);

	if ( isKeyDown( app::KeyEvent::KEY_SPACE )) {
		return false;
	}

	_cutting = true;
	_cutStart = _cutEnd = _mouseWorld;

	return true;
}

bool MouseCutterComponent::mouseUp( const ci::app::MouseEvent &event ){
	if (_cutting) {
		if (_terrain) {
			const float radius = _radius / getLevel()->getViewport()->getScale();
			_terrain->getWorld()->cut(_cutStart, _cutEnd, radius, _cutFilter);
		}

		_cutting = false;
	}
	return false;
}

bool MouseCutterComponent::mouseMove( const ci::app::MouseEvent &event, const ivec2 &delta ) {
	_mouseScreen = event.getPos();
	_mouseWorld = getLevel()->getViewport()->screenToWorld(_mouseScreen);
	return false;
}

bool MouseCutterComponent::mouseDrag( const ci::app::MouseEvent &event, const ivec2 &delta ) {
	_mouseScreen = event.getPos();
	_mouseWorld = getLevel()->getViewport()->screenToWorld(_mouseScreen);
	if (_cutting) {
		_cutEnd = _mouseWorld;
	}
	return false;
}

#pragma mark - MouseCutterDrawComponent

MouseCutterDrawComponent::MouseCutterDrawComponent(ColorA color):
_color(color)
{}

void MouseCutterDrawComponent::onReady(core::GameObjectRef parent, core::LevelRef level){
	DrawComponent::onReady(parent,level);
	_cutterComponent = getSibling<MouseCutterComponent>();
}

void MouseCutterDrawComponent::draw(const core::render_state &renderState){
	if (shared_ptr<MouseCutterComponent> cc = _cutterComponent.lock()) {
		if (cc->isCutting()) {
			float radius = cc->getRadius() / renderState.viewport->getScale();
			vec2 dir = cc->getCutEnd() - cc->getCutStart();
			float len = ::length(dir);
			if (len > 1e-2) {
				dir /= len;
				float angle = atan2(dir.y, dir.x);
				vec2 center = (cc->getCutStart()+cc->getCutEnd()) * 0.5;

				mat4 M = translate(vec3(center.x, center.y, 0)) * rotate(angle, vec3(0,0,1));
				gl::ScopedModelMatrix smm;
				gl::multModelMatrix(M);

				gl::color(_color);
				gl::drawSolidRect(Rectf(-len/2, -radius, +len/2, +radius));
			}
		}

	}
}
