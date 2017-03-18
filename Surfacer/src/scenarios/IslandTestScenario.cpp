//
//  IslandTestScenario.cpp
//  Milestone6
//
//  Created by Shamyl Zakariya on 3/5/17.
//
//

#include "IslandTestScenario.hpp"
#include "GameApp.hpp"

#include <cinder/Rand.h>

namespace {

	const float CUT_WIDTH = 4;

	PolyLine2f rect(float left, float bottom, float right, float top) {
		PolyLine2f pl;
		pl.push_back(vec2(left,bottom));
		pl.push_back(vec2(right,bottom));
		pl.push_back(vec2(right,top));
		pl.push_back(vec2(left,top));
		pl.setClosed();
		return pl;
	}

	PolyLine2f rect(vec2 center, vec2 size) {
		return rect(center.x - size.x/2, center.y - size.y/2, center.x + size.x/2, center.y + size.y/2);
	}

	island::ShapeRef boxShape(vec2 center, vec2 size) {
		return island::Shape::fromContour(rect(center.x - size.x/2, center.y - size.y/2, center.x + size.x/2, center.y + size.y/2));
	}

	namespace Categories {
		enum Categories {
			ISLAND = 1 << 30,
			CUTTER = 1 << 29,
			PICK = 1 << 28
		};
	};

	namespace Filters {
		cpShapeFilter ISLAND = cpShapeFilterNew(CP_NO_GROUP, Categories::ISLAND, Categories::ISLAND | Categories::CUTTER | Categories::PICK);
		cpShapeFilter CUTTER = cpShapeFilterNew(CP_NO_GROUP, Categories::CUTTER, Categories::ISLAND);
		cpShapeFilter PICK = cpShapeFilterNew(CP_NO_GROUP, Categories::PICK, Categories::ISLAND);
	}


}

/*
	bool _cutting;
	vec2 _cutterStart, _cutterEnd, _mouseScreen, _mouseWorld;

	ViewportController _cameraController;
 
	island::WorldRef _islandWorld;

	cpSpace *_space;
	cpBody *_mouseBody, *_draggingBody;
	cpConstraint *_mouseJoint;
*/

IslandTestScenario::IslandTestScenario():
_cameraController(camera()),
_cutting(false),
_space(nullptr),
_mouseBody(nullptr),
_draggingBody(nullptr),
_mouseJoint(nullptr)
{
	_cameraController.setConstraintMask( ViewportController::ConstrainPan | ViewportController::ConstrainScale );
}

IslandTestScenario::~IslandTestScenario(){}

void IslandTestScenario::setup() {

	//timeSpatialIndex();

	_space = cpSpaceNew();
	cpSpaceSetDamping(_space, 0.95);
	_mouseBody = cpBodyNewKinematic();

	//testBasicIslandSetup();
	//testComplexIslandSetup();
	//testSimpleAnchors();
	testComplexAnchors();
}

void IslandTestScenario::cleanup() {
	_islandWorld.reset();

	_cutting = false;

	if (_mouseJoint) {
		cpSpaceRemoveConstraint(_space, _mouseJoint);
		cpConstraintFree(_mouseJoint);
		_mouseJoint = nullptr;
		_draggingBody = nullptr;
	}

	cpBodyFree(_mouseBody);
	cpSpaceFree(_space);
}

void IslandTestScenario::resize( ivec2 size ){}

void IslandTestScenario::step( const time_state &time ) {
	cpSpaceStep(_space, time.deltaT);

	cpVect mouseBodyPos = cpv(_mouseWorld);
	cpVect newMouseBodyPos = cpvlerp(cpBodyGetPosition(_mouseBody), mouseBodyPos, 0.75);

	cpBodySetVelocity(_mouseBody, cpvmult(cpvsub(newMouseBodyPos, cpBodyGetPosition(_mouseBody)), time.deltaT));
	cpBodySetPosition(_mouseBody, newMouseBodyPos);

	if (_islandWorld) {
		_islandWorld->step(time);
	}
}

void IslandTestScenario::update( const time_state &time ) {
	_cameraController.step(time);
	if (_islandWorld) {
		_islandWorld->update(time);
	}
}

void IslandTestScenario::draw( const render_state &state ) {
	//const ivec2 screenSize = getWindowSize();
	gl::clear( Color( 0.2, 0.2, 0.2 ) );

	{
		// apply camera modelview
		Viewport::ScopedState cameraState(camera());

		drawWorldCoordinateSystem(state);

		if (_islandWorld) {
			_islandWorld->draw(state);
		}

		if (_cutting) {
			float radius = (CUT_WIDTH/2) / state.viewport.getScale();
			vec2 dir = _cutterEnd - _cutterStart;
			float len = ::length(dir);
			if (len > 1e-2) {
				dir /= len;
				float angle = atan2(dir.y, dir.x);
				vec2 center = (_cutterStart+_cutterEnd)/2.f;

				mat4 M = translate(vec3(center.x, center.y, 0)) * rotate(angle, vec3(0,0,1));
				gl::ScopedModelMatrix smm;
				gl::multModelMatrix(M);

				gl::color(Color(1,0,1));
				gl::drawSolidRect(Rectf(-len/2, -radius, +len/2, +radius));
			}
		}

		if (_mouseJoint) {
			// we're dragging, so draw mouse body
			const float radius = 2 * camera().getReciprocalScale();
			gl::color(Color(1,0,1));
			gl::drawSolidCircle(v2(cpBodyGetPosition(_mouseBody)), radius);
		}
	}

	// now we're in screen space


	// draw fpf/sps
	float fps = core::GameApp::get()->getAverageFps();
	float sps = core::GameApp::get()->getAverageSps();
	string info = core::strings::format("%.1f %.1f", fps, sps);
	gl::drawString(info, vec2(10,10), Color(1,1,1));
}

bool IslandTestScenario::mouseDown( const ci::app::MouseEvent &event ) {
	releaseMouseDragConstraint();

	_mouseScreen = event.getPos();
	_mouseWorld = camera().screenToWorld(_mouseScreen);

	if ( event.isAltDown() )
	{
		_cameraController.lookAt( _mouseWorld );
		return true;
	}

	if ( isKeyDown( app::KeyEvent::KEY_SPACE )) {
		return false;
	}

	const float distance = 1.f;
	cpPointQueryInfo info = {};
	cpShape *pick = cpSpacePointQueryNearest(_space, cpv(_mouseWorld), distance, Filters::PICK, &info);
	if (pick) {
		cpBody *pickBody = cpShapeGetBody(pick);

		if (pickBody && cpBodyGetType(pickBody) == CP_BODY_TYPE_DYNAMIC) {
			island::Body *islandBody = (island::Body*) cpBodyGetUserData(pickBody);
			CI_LOG_D("Attaching mouse joint to body: " << pickBody << " shape: " << islandBody->getName());

			cpVect nearest = (info.distance > 0.0f ? info.point : cpv(_mouseWorld));

			_draggingBody = pickBody;
			_mouseJoint = cpPivotJointNew2(_mouseBody, _draggingBody, cpvzero, cpBodyWorldToLocal(_draggingBody,nearest));

			//cpConstraintSetMaxForce(_mouseJoint, cpBodyGetMass(_draggingBody) * length(v2(cpSpaceGetGravity(_space))) * 1000);
			//cpConstraintSetMaxBias(_mouseJoint, cpfpow(1.0f - 0.15f, 60.0f)); // magic numbers from ChipmunkDemo.c

			cpSpaceAddConstraint(_space, _mouseJoint);

			return true;
		}
	}

	_cutting = true;
	_cutterStart = _cutterEnd = _mouseWorld;

	return true;
}

bool IslandTestScenario::mouseUp( const ci::app::MouseEvent &event ) {
	releaseMouseDragConstraint();

	if (_cutting) {
		if (_islandWorld) {
			const float radius = (CUT_WIDTH/2) / _cameraController.getScale();
			_islandWorld->cut(_cutterStart, _cutterEnd, radius);
		}

		_cutting = false;
	}

	return true;
}

bool IslandTestScenario::mouseWheel( const ci::app::MouseEvent &event ){
	float zoom = _cameraController.getScale(),
	wheelScale = 0.1 * zoom,
	dz = (event.getWheelIncrement() * wheelScale);

	_cameraController.setScale( std::max( zoom + dz, 0.1f ), event.getPos() );

	return true;
}

bool IslandTestScenario::mouseMove( const ci::app::MouseEvent &event, const vec2 &delta ) {
	_mouseScreen = event.getPos();
	_mouseWorld = camera().screenToWorld(_mouseScreen);
	return true;
}

bool IslandTestScenario::mouseDrag( const ci::app::MouseEvent &event, const vec2 &delta ) {
	_mouseScreen = event.getPos();
	_mouseWorld = camera().screenToWorld(_mouseScreen);

	if ( isKeyDown( app::KeyEvent::KEY_SPACE ))
	{
		_cameraController.setPan( _cameraController.getPan() + delta );
		_cutting = false;
		return true;
	}


	if (_cutting) {
		_cutterEnd = _mouseWorld;
	}

	return true;
}

bool IslandTestScenario::keyDown( const ci::app::KeyEvent &event ) {
	if (event.getChar() == 'r') {
		reset();
		return true;
	}
	return false;
}

bool IslandTestScenario::keyUp( const ci::app::KeyEvent &event ) {
	return false;
}

void IslandTestScenario::reset() {
	cleanup();
	setup();
}

void IslandTestScenario::testBasicIslandSetup() {
	_cameraController.lookAt(vec2(0,0));

	island::material mat;
	mat.density = 1;
	mat.friction = 0.5;
	mat.filter = Filters::ISLAND;

	_islandWorld = make_shared<island::World>(_space,mat);

	vector<island::ShapeRef> shapes = {
//		island::Shape::fromContour(rect(0, 0, 100, 50)),		// 0
//		island::Shape::fromContour(rect(100,0, 150, 50)),		// 1 to right of 0 - binds to 0
//		island::Shape::fromContour(rect(-100,0, 0, 50)),		// 2 to left of 0 - binds to 0
		island::Shape::fromContour(rect(-10, 50, 110, 100)),	// 3 above 0, but wider
		island::Shape::fromContour(rect(-10, 100, 110, 200)), // 4 above 3, binds to 3

		//island::Shape::fromContour(rect(200,0,210,0)),		// empty rect - should be garbage collected
	};

	_islandWorld->build(shapes);
}

void IslandTestScenario::testComplexIslandSetup() {
	_cameraController.lookAt(vec2(0,0));

	island::material mat;
	mat.density = 1;
	mat.friction = 0.5;
	mat.filter = Filters::ISLAND;

	_islandWorld = make_shared<island::World>(_space,mat);

	const vec2 boxSize(50,50);
	auto boxPos = [boxSize](float x, float y)->vec2 {
		return vec2(x * boxSize.x + boxSize.x/2,y * boxSize.y + boxSize.y/2);
	};

	vector<island::ShapeRef> shapes = {
		boxShape(boxPos(0,0),boxSize),
		boxShape(boxPos(1,0),boxSize),
		boxShape(boxPos(2,0),boxSize),
		boxShape(boxPos(3,0),boxSize),
		boxShape(boxPos(3,1),boxSize),
		boxShape(boxPos(3,2),boxSize),
		boxShape(boxPos(3,3),boxSize),
		boxShape(boxPos(2,3),boxSize),
		boxShape(boxPos(1,3),boxSize),
		boxShape(boxPos(0,3),boxSize),
		boxShape(boxPos(0,2),boxSize),
		boxShape(boxPos(0,1),boxSize),
	};

	_islandWorld->build(shapes);
}

void IslandTestScenario::testSimpleAnchors() {
	_cameraController.lookAt(vec2(0,0));

	island::material mat;
	mat.density = 1;
	mat.friction = 0.5;
	mat.filter = Filters::ISLAND;

	_islandWorld = make_shared<island::World>(_space,mat);

	vector<island::ShapeRef> shapes = {
		island::Shape::fromContour(rect(0, 0, 100, 50)),		// 0
		island::Shape::fromContour(rect(100,0, 150, 50)),		// 1 to right of 0 - binds to 0
		island::Shape::fromContour(rect(-100,0, 0, 50)),		// 2 to left of 0 - binds to 0
	};

	vector<island::AnchorRef> anchors = {
		island::Anchor::fromContour(rect(40,20,60,30))
	};

	_islandWorld->build(shapes, anchors);
}

void IslandTestScenario::testComplexAnchors() {
	_cameraController.lookAt(vec2(0,0));

	island::material mat;
	mat.density = 1;
	mat.friction = 0.5;
	mat.filter = Filters::ISLAND;

	cpSpaceSetGravity(_space, cpv(0,-9.8 * 10));
	_islandWorld = make_shared<island::World>(_space,mat);

	const vec2 boxSize(50,50);
	auto boxPos = [boxSize](float x, float y)->vec2 {
		return vec2(x * boxSize.x + boxSize.x/2,y * boxSize.y + boxSize.y/2);
	};

	vector<island::ShapeRef> shapes = {
		boxShape(boxPos(0,0),boxSize),
		boxShape(boxPos(1,0),boxSize),
		boxShape(boxPos(2,0),boxSize),
		boxShape(boxPos(3,0),boxSize),
		boxShape(boxPos(3,1),boxSize),
		boxShape(boxPos(3,2),boxSize),
		boxShape(boxPos(3,3),boxSize),
		boxShape(boxPos(2,3),boxSize),
		boxShape(boxPos(1,3),boxSize),
		boxShape(boxPos(0,3),boxSize),
		boxShape(boxPos(0,2),boxSize),
		boxShape(boxPos(0,1),boxSize),

		boxShape(boxSize * 2.f, boxSize)
	};

	vector<island::AnchorRef> anchors = {
		island::Anchor::fromContour(rect(vec2(25,25), vec2(10,10)))
	};

	_islandWorld->build(shapes, anchors);
}

void IslandTestScenario::timeSpatialIndex() {

	ci::Rand rng;

	auto generator = [&rng](cpBB bounds, int count) -> vector<cpBB> {
		vector<cpBB> out;

		const float sizeMin = (bounds.r - bounds.l) / 50;
		const float sizeMax = sizeMin * 2;

		for (int i = 0; i < count; i++) {
			vec2 origin = vec2(rng.nextFloat(bounds.l, bounds.r), rng.nextFloat(bounds.b, bounds.t));
			float size = rng.nextFloat(sizeMin, sizeMax);

			cpBB bb = cpBBNew(origin.x-size/2, origin.y-size/2, origin.x+size/2, origin.y+size/2);
			out.push_back(bb);
		}

		return out;
	};

	auto timeUsingSpatialIndex = [](const vector<cpBB> bbs, core::SpatialIndex<int> &index) -> double {
		core::StopWatch timer;
		index.clear();

		int tick = 0;
		for (auto bb : bbs) {
			index.insert(bb, tick++);
		}

		int count = 0;

		for (auto queryBB : bbs) {
			auto neighbors = index.sweep(queryBB);
			count += neighbors.size();
		}

		return timer.mark();
	};

	auto timeUsingBruteForce = [](const vector<cpBB> bbs) -> double {
		core::StopWatch timer;

		int count = 0;
		const auto last = bbs.end();
		for (auto it = bbs.begin(); it != last; ++it) {
			for (auto it2 = bbs.begin(); it2 != last; ++it2) {
				if (it != it2 && cpBBIntersects(*it, *it2)) {
					count ++;
				}
			}
		}

		double m = timer.mark();
		app::console() << "timeUsingBruteForce count: " << count << std::endl;
		return m;
	};

	auto performTimingRun = [&generator, &timeUsingSpatialIndex, &timeUsingBruteForce](int count) {
		const cpBB bounds = cpBBNew(-1000, -1000, 1000, 10000);
		vector<cpBB> bbs = generator(bounds, count);
		core::SpatialIndex<int> index(50);
		const int runs = 10;

		double sumSpatialIndex = 0;
		double sumBruteForce = 0;

		for (int i = 0; i < runs; i++) {
			sumSpatialIndex += timeUsingSpatialIndex(bbs, index);
			sumBruteForce += timeUsingBruteForce(bbs);
		}

		const double spatialIndexAverageTime = sumSpatialIndex / runs;
		const double bruteForceAverageTime = sumBruteForce / runs;

		app::console() << "For " << runs << " runs over " << count << " items:" << endl;
		app::console() << "\tspatialIndex average time: " << spatialIndexAverageTime << " seconds" << endl;
		app::console() << "\tbruteForce average time: " << bruteForceAverageTime << " seconds" << endl;
		app::console() << "\tratio spatialIndex/bruteForce: " << (spatialIndexAverageTime/bruteForceAverageTime) << endl;
		app::console() << endl << endl;
	};

	app::console() << "------------------------------------" << endl << "PERFORMING PERF MEASUREMENTS" << endl;
	performTimingRun(450);

//	for (int i = 4; i < 100; i+= 4) {
//		performTimingRun(i);
//	}

}

void IslandTestScenario::drawWorldCoordinateSystem(const render_state &state) {

	const cpBB frustum = camera().getFrustum();
	const int gridSize = 10;
	const int majorGridSize = 100;
	const int firstY = static_cast<int>(floor(frustum.b / gridSize) * gridSize);
	const int lastY = static_cast<int>(floor(frustum.t / gridSize) * gridSize) + gridSize;
	const int firstX = static_cast<int>(floor(frustum.l / gridSize) * gridSize);
	const int lastX = static_cast<int>(floor(frustum.r / gridSize) * gridSize) + gridSize;

	const auto MinorLineColor = ColorA(1,1,1,0.05);
	const auto MajorLineColor = ColorA(1,1,1,0.25);
	const auto AxisColor = ColorA(1,0,0,1);
	gl::lineWidth(1.0 / camera().getScale());

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

void IslandTestScenario::releaseMouseDragConstraint() {
	if (_mouseJoint) {
		cpSpaceRemoveConstraint(_space, _mouseJoint);
		cpConstraintFree(_mouseJoint);
		_mouseJoint = nullptr;
		_draggingBody = nullptr;
	}
}
