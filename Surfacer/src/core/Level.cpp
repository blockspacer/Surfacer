//
//  Level.cpp
//  Surfacer
//
//  Created by Shamyl Zakariya on 3/27/17.
//
//

#include "Level.hpp"

#include <cinder/CinderAssert.h>

#include "ChipmunkHelpers.hpp"
#include "Scenario.hpp"

namespace core {

#pragma mark - SpaceAccess

	SpaceAccess::SpaceAccess(cpSpace *space, Level *level):
	_space(space),
	_level(level)
	{}

	void SpaceAccess::addBody(cpBody *body) {
		cpSpaceAddBody(_space, body);
		bodyWasAddedToSpace(body);
	}

	void SpaceAccess::addShape(cpShape *shape) {
		cpSpaceAddShape(_space, shape);
		shapeWasAddedToSpace(shape);
	}

	void SpaceAccess::addConstraint(cpConstraint *constraint) {
		cpSpaceAddConstraint(_space, constraint);
		constraintWasAddedToSpace(constraint);
	}

	SpaceAccess::gravitation SpaceAccess::getGravity(const dvec2 &world) {
		return _level->getGravitation(world);
	}


#pragma mark - DrawDispatcher

	namespace {

		cpHashValue getCPHashValue(const DrawComponentRef &dc) {
			return reinterpret_cast<cpHashValue>(dc.get());
		}

		cpHashValue getCPHashValue(DrawComponent *dc) {
			return reinterpret_cast<cpHashValue>(dc);
		}

		cpBB objectBBFunc( void *obj )
		{
			return static_cast<DrawComponent*>(obj)->getBB();
		}

		cpCollisionID visibleObjectCollector(void *obj1, void *obj2, cpCollisionID id, void *data) {
			//DrawDispatcher *dispatcher = static_cast<DrawDispatcher*>(obj1);
			DrawComponentRef dc = static_cast<DrawComponent*>(obj2)->shared_from_this_as<DrawComponent>();
			DrawDispatcher::collector *collector = static_cast<DrawDispatcher::collector*>(data);

			//
			//	We don't want dupes in sorted vector, so filter based on whether
			//	the object made it into the set
			//

			if( collector->visible.insert(dc).second )
			{
				collector->sorted.push_back(dc);
			}

			return id;
		}

		bool visibleObjectDisplaySorter( const DrawComponentRef &a, const DrawComponentRef &b )
		{
			//
			// draw lower layers before higher layers
			//

			if ( a->getLayer() != b->getLayer() )
			{
				return a->getLayer() < b->getLayer();
			}

			//
			//	If we're here, the objects are on the same layer. So now
			//	we want to group objects with the same batchDrawDelegate
			//

			if ( a->getBatchDrawDelegate() != b->getBatchDrawDelegate() )
			{
				return a->getBatchDrawDelegate() < b->getBatchDrawDelegate();
			}

			//
			//	If we're here, the two objects have the same ( possibly null )
			//	batchDrawDelegate, so, just order them from older to newer
			//

			return a->getObject()->getId() < b->getObject()->getId();
		}
		
	}

	/*
		 cpSpatialIndex *_index;
		 set<DrawComponentRef> _all, _alwaysVisible, _deferredSpatialIndexInsertion;
		 map<size_t, set<DrawComponentRef>> _drawComponentsById;
		 collector _collector;
	 */

	DrawDispatcher::DrawDispatcher():
	_index(cpBBTreeNew( objectBBFunc, NULL ))
	{}

	DrawDispatcher::~DrawDispatcher()
	{
		cpSpatialIndexFree( _index );
	}

	void DrawDispatcher::add( size_t id, const DrawComponentRef &dc )
	{
		_drawComponentsById[id].insert(dc);

		if (_all.insert(dc).second) {
			switch( dc->getVisibilityDetermination() )
			{
				case VisibilityDetermination::ALWAYS_DRAW:
					_alwaysVisible.insert( dc );
					break;

				case VisibilityDetermination::FRUSTUM_CULLING:
					// wait until ::cull to add to spatial index. This is to accommodate
					// partially constructed Objects
					_deferredSpatialIndexInsertion.insert(dc);
					break;

				case VisibilityDetermination::NEVER_DRAW:
					break;
			}
		}
	}

	void DrawDispatcher::remove(size_t id) {
		for (auto &dc : _drawComponentsById[id]) {
			remove(dc);
		}
	}

	void DrawDispatcher::remove( const DrawComponentRef &dc )
	{
		if (_all.erase(dc)) {
			switch( dc->getVisibilityDetermination() )
			{
				case VisibilityDetermination::ALWAYS_DRAW:
				{
					_alwaysVisible.erase( dc );
					break;
				}

				case VisibilityDetermination::FRUSTUM_CULLING:
				{
					_deferredSpatialIndexInsertion.erase(dc);

					const auto dcp = dc.get();
					const auto hv = getCPHashValue(dc);
					if (cpSpatialIndexContains(_index, dcp, hv)){
						cpSpatialIndexRemove( _index, dcp, hv);
					}
					break;
				}

				case VisibilityDetermination::NEVER_DRAW:
					break;
			}

			_collector.remove(dc);
		}
	}

	void DrawDispatcher::moved( const DrawComponentRef &dc )
	{
		moved(dc.get());
	}

	void DrawDispatcher::moved( DrawComponent *dc ) {
		if ( dc->getVisibilityDetermination() == VisibilityDetermination::FRUSTUM_CULLING )
		{
			cpSpatialIndexReindexObject( _index, dc, getCPHashValue(dc));
		}
	}

	void DrawDispatcher::cull( const render_state &state )
	{
		// if we have any deferred insertions to spatial index, do it now
		if (!_deferredSpatialIndexInsertion.empty())
		{
			for (const auto &dc : _deferredSpatialIndexInsertion)
			{
				cpSpatialIndexInsert( _index, dc.get(), getCPHashValue(dc) );
			}
			_deferredSpatialIndexInsertion.clear();
		}


		//
		//	clear storage - note: std::vector doesn't free, it keeps space reserved.
		//

		_collector.visible.clear();
		_collector.sorted.clear();

		//
		//	Copy over objects which are always visible
		//

		for (const auto &dc : _alwaysVisible) {
			_collector.visible.insert(dc);
			_collector.sorted.push_back(dc);
		}

		//
		//	Find those objects which use frustum culling and which intersect the current view frustum
		//

		cpSpatialIndexQuery( _index, this, state.viewport->getFrustum(), visibleObjectCollector, &_collector );

		//
		//	Sort them all
		//

		std::sort(_collector.sorted.begin(), _collector.sorted.end(), visibleObjectDisplaySorter );
	}

	void DrawDispatcher::draw( const render_state &state )
	{
		render_state renderState = state;

		for( vector<DrawComponentRef>::iterator
			dcIt(_collector.sorted.begin()),
			end(_collector.sorted.end());
			dcIt != end;
			++dcIt )
		{
			DrawComponentRef dc = *dcIt;
			DrawComponent::BatchDrawDelegateRef delegate = dc->getBatchDrawDelegate();
			int drawPasses = dc->getDrawPasses();

			vector<DrawComponentRef>::iterator newDcIt = dcIt;
			for( renderState.pass = 0; renderState.pass < drawPasses; ++renderState.pass ) {
				if ( delegate ) {
					newDcIt = _drawDelegateRun(dcIt, end, renderState );
				}
				else
				{
					dc->draw( renderState );
				}
			}

			dcIt = newDcIt;
		}
	}

	vector<DrawComponentRef>::iterator
	DrawDispatcher::_drawDelegateRun(vector<DrawComponentRef>::iterator firstInRun, vector<DrawComponentRef>::iterator storageEnd, const render_state &state ) {
		vector<DrawComponentRef>::iterator dcIt = firstInRun;
		DrawComponentRef dc = *dcIt;
		DrawComponent::BatchDrawDelegateRef delegate = dc->getBatchDrawDelegate();

		delegate->prepareForBatchDraw( state, dc );

		for( ; dcIt != storageEnd; ++dcIt )
		{
			//
			//	If the delegate run has completed, clean up after our run
			//	and return the current iterator.
			//

			if ( (*dcIt)->getBatchDrawDelegate() != delegate )
			{
				delegate->cleanupAfterBatchDraw( state, *firstInRun, dc );
				return dcIt - 1;
			}
			
			dc = *dcIt;
			dc->draw( state );
		} 
		
		//
		//	If we reached the end of storage, run cleanup
		//
		
		delegate->cleanupAfterBatchDraw( state, *firstInRun, dc );
		
		return dcIt - 1;
	}

#pragma mark - Level

	namespace {

		void radialGravityVelocityFunc(cpBody *body, cpVect gravity, cpFloat damping, cpFloat dt) {
			cpSpace *space = cpBodyGetSpace(body);
			Level *level = static_cast<Level*>(cpSpaceGetUserData(space));
			auto g = level->getGravitation(v2(cpBodyGetPosition(body)));
			auto force = g.force;

			// objects can define a custom gravity modifier - 0 means no effect, -1 would be repulsive, etc
			if (ObjectRef object = cpBodyGetObject(body)) {
				if (PhysicsComponentRef physics = object->getPhysicsComponent()) {
					force *= physics->getGravityModifier();
				}
			}

			cpBodyUpdateVelocity(body, cpv(g.dir * force), damping, dt);
		}

	}

	/*
		cpSpace *_space;
		SpaceAccessRef _spaceAccess;
		bool _ready, _paused;
		ScenarioWeakRef _scenario;
		set<ObjectRef> _objects;
		map<size_t,ObjectRef> _objectsById;
		time_state _time;
		string _name;
		DrawDispatcherRef _drawDispatcher;
		cpBodyVelocityFunc _bodyVelocityFunc;
	 */

	Level::Level(string name):
	_space(cpSpaceNew()),
	_ready(false),
	_paused(false),
	_time(0,0,0),
	_name(name),
	_drawDispatcher(make_shared<DrawDispatcher>()),
	_bodyVelocityFunc(cpBodyUpdateVelocity)
	{
		// some defaults
		cpSpaceSetIterations( _space, 30 );
		cpSpaceSetDamping( _space, 0.95 );
		cpSpaceSetSleepTimeThreshold( _space, 1 );
		cpSpaceSetUserData(_space, this);

		_spaceAccess = SpaceAccessRef(new SpaceAccess(_space, this));
		_spaceAccess->bodyWasAddedToSpace.connect(this, &Level::onBodyAddedToSpace);
		_spaceAccess->shapeWasAddedToSpace.connect(this, &Level::onShapeAddedToSpace);
		_spaceAccess->constraintWasAddedToSpace.connect(this, &Level::onConstraintAddedToSpace);

		setGravityType(DIRECTIONAL);
		setDirectionalGravityDirection(dvec2(0,-9.8));
	}

	Level::~Level() {
		// these all have to be freed before we can free the space
		_objects.clear();
		_objectsById.clear();
		_drawDispatcher.reset();

		cpSpaceFree(_space);
	}

	void Level::setPaused(bool paused) {
		if (paused != isPaused()) {
			_paused = isPaused();
			if (isPaused()) {
				signals.onLevelPaused(shared_from_this_as<Level>());
			} else {
				signals.onLevelUnpaused(shared_from_this_as<Level>());
			}
		}
	}

	void Level::resize( ivec2 newSize )
	{}

	void Level::step( const time_state &time ) {
		if (!_ready) {
			onReady();
		}

		if (!_paused) {
			// step the chipmunk space
			cpSpaceStep(_space, time.deltaT);

			// dispatch any synthetic contacts which were generated
			dispatchSyntheticContacts();
		}

		if (!_paused) {
			vector<ObjectRef> moribund;
			for (auto &obj : _objects) {
				if (!obj->isFinished()) {
					obj->step(time);
				} else {
					moribund.push_back(obj);
				}
			}

			if (!moribund.empty()) {
				for (auto &obj : moribund) {
					removeObject(obj);
				}
			}
		}
	}

	void Level::update( const time_state &time ) {
		if (!_paused) {
			_time = time;
		}

		if (!_ready) {
			onReady();
		}

		if (!_paused) {
			vector<ObjectRef> moribund;
			for (auto &obj : _objects) {
				if (!obj->isFinished()) {
					obj->update(time);
				} else {
					moribund.push_back(obj);
				}
			}

			if (!moribund.empty()) {
				for (auto &obj : moribund) {
					removeObject(obj);
				}
			}
		}

		// cull visibility sets
		if (ScenarioRef scenario = _scenario.lock()) {
			_drawDispatcher->cull(scenario->getRenderState());
		}
	}

	void Level::draw( const render_state &state ) {
		//
		//	This may look odd, but Level is responsible for Pausing, not the owner Scenario. This means that
		//	when the game is paused, the level is responsible for sending a mock 'paused' time object that reflects
		//	the time when the game was paused. I can't do this at the Scenario level because Scenario may have UI
		//	which needs correct time updates.
		//

		render_state localState = state;
		localState.time = _time.time;
		localState.deltaT = _time.deltaT;

		_drawDispatcher->draw( localState );
	}

	void Level::drawScreen( const render_state &state ) {
		for (const auto &dc : _drawDispatcher->all()) {
			dc->drawScreen(state);
		}
	}

	void Level::addObject(ObjectRef obj) {
		CI_ASSERT_MSG(!obj->getLevel(), "Can't add a Object that already has been added to this or another Level");

		size_t id = obj->getId();

		_objects.insert(obj);
		_objectsById[id] = obj;

		for (auto &dc : obj->getDrawComponents()) {
			_drawDispatcher->add(id, dc);
		}

		obj->onAddedToLevel(shared_from_this_as<Level>());

		if (_ready) {
			obj->onReady(shared_from_this_as<Level>());
		}

	}

	void Level::removeObject(ObjectRef obj) {
		CI_ASSERT_MSG(obj->getLevel().get() == this, "Can't remove a Object which isn't a child of this Level");

		size_t id = obj->getId();

		_objects.erase(obj);
		_objectsById.erase(id);
		_drawDispatcher->remove(id);

		obj->onRemovedFromLevel();
	}

	ObjectRef Level::getObjectById(size_t id) const {
		auto pos = _objectsById.find(id);
		if (pos != _objectsById.end()) {
			return pos->second;
		} else {
			return nullptr;
		}
	}

	vector<ObjectRef> Level::getObjectsByName(string name) const {

		vector<ObjectRef> result;
		copy_if(_objects.begin(), _objects.end(), back_inserter(result),[&name](const ObjectRef &object) -> bool {
			return object->getName() == name;
		});

		return result;
	}

	ViewportRef Level::getViewport() const {
		return getScenario()->getViewport();
	}

	ViewportControllerRef Level::getViewportController() const {
		return getScenario()->getViewportController();
	}

	void Level::setGravityType(GravityType type) {
		_gravityType = type;
		switch(_gravityType) {
			case DIRECTIONAL:
				setCpBodyVelocityUpdateFunc(cpBodyUpdateVelocity);
				break;
			case RADIAL:
				setCpBodyVelocityUpdateFunc(radialGravityVelocityFunc);
				break;
		}
	}

	void Level::setDirectionalGravityDirection(dvec2 dir) {
		_directionalGravityDir = dir;
		cpSpaceSetGravity(_space, cpv(_directionalGravityDir));
	}

	void Level::setRadialGravity(radial_gravity_info rgi) {
		_radialGravityInfo = rgi;
	}

	SpaceAccess::gravitation Level::getGravitation(dvec2 world) const {
		switch(_gravityType) {
			case RADIAL: {
				dvec2 p_2_com = _radialGravityInfo.centerOfMass - world;
				double dist = length(p_2_com);
				if (dist > 1e-5) {
					p_2_com /= dist;
					dvec2 g = p_2_com * (_radialGravityInfo.strength / pow(dist, _radialGravityInfo.falloffPower));
					double f = length(g);
					return SpaceAccess::gravitation(g/f, f);
				}
				return SpaceAccess::gravitation(dvec2(0,0), 0);
			}

			case Level::DIRECTIONAL:
				dvec2 g = v2(cpSpaceGetGravity(_space));
				double f = length(g);
				return SpaceAccess::gravitation(g/f, f);
		}
	}


	namespace detail {

		cpBool Level_collisionBeginHandler(cpArbiter *arb, struct cpSpace *space, cpDataPointer data) {
			Level *level = static_cast<Level*>(data);

			cpShape *a = nullptr, *b = nullptr;
			cpArbiterGetShapes(arb, &a, &b);

			// if any handlers are defined for this pairing, dispatch
			cpBool accept = cpTrue;
			const Level::collision_type_pair ctp(cpShapeGetCollisionType(a),cpShapeGetCollisionType(b));
			auto pos = level->_collisionBeginHandlers.find(ctp);
			if (pos != level->_collisionBeginHandlers.end()) {
				ObjectRef ga = cpShapeGetObject(a);
				ObjectRef gb = cpShapeGetObject(b);
				for (const auto &cb : pos->second) {
					if (!cb(ctp, ga, gb, arb)) {
						accept = cpFalse;
					}
				}
			}

			if (!level->onCollisionBegin(arb)) {
				accept = cpFalse;
			}

			return accept;
		}

		cpBool Level_collisionPreSolveHandler(cpArbiter *arb, struct cpSpace *space, cpDataPointer data) {
			Level *level = static_cast<Level*>(data);

			cpShape *a = nullptr, *b = nullptr;
			cpArbiterGetShapes(arb, &a, &b);

			// if any handlers are defined for this pairing, dispatch
			cpBool accept = cpTrue;
			const Level::collision_type_pair ctp(cpShapeGetCollisionType(a),cpShapeGetCollisionType(b));
			auto pos = level->_collisionPreSolveHandlers.find(ctp);
			if (pos != level->_collisionPreSolveHandlers.end()) {
				ObjectRef ga = cpShapeGetObject(a);
				ObjectRef gb = cpShapeGetObject(b);
				for (const auto &cb : pos->second) {
					if (!cb(ctp, ga, gb, arb)) {
						accept = cpFalse;
					}
				}
			}

			if (!level->onCollisionPreSolve(arb)) {
				accept = cpFalse;
			}

			return accept;
		}

		void Level_collisionPostSolveHandler(cpArbiter *arb, struct cpSpace *space, cpDataPointer data) {
			Level *level = static_cast<Level*>(data);

			cpShape *a = nullptr, *b = nullptr;
			cpArbiterGetShapes(arb, &a, &b);

			// if any handlers are defined for this pairing, dispatch
			const Level::collision_type_pair ctp(cpShapeGetCollisionType(a),cpShapeGetCollisionType(b));
			ObjectRef ga = cpShapeGetObject(a);
			ObjectRef gb = cpShapeGetObject(b);

			// dispatch to post solve handlers
			{
				auto pos = level->_collisionPostSolveHandlers.find(ctp);
				if (pos != level->_collisionPostSolveHandlers.end()) {
					for (const auto &cb : pos->second) {
						cb(ctp, ga, gb, arb);
					}
				}
			}

			// dispatch to generic contact handlers
			{
				auto pos = level->_contactHandlers.find(ctp);
				if (pos != level->_contactHandlers.end()) {
					for (const auto &cb : pos->second) {
						cb(ctp, ga, gb);
					}
				}
			}

			level->onCollisionPostSolve(arb);
		}

		void Level_collisionSeparateHandler(cpArbiter *arb, struct cpSpace *space, cpDataPointer data) {
			Level *level = static_cast<Level*>(data);

			cpShape *a = nullptr, *b = nullptr;
			cpArbiterGetShapes(arb, &a, &b);

			// if any handlers are defined for this pairing, dispatch
			const Level::collision_type_pair ctp(cpShapeGetCollisionType(a),cpShapeGetCollisionType(b));
			auto pos = level->_collisionSeparateHandlers.find(ctp);
			if (pos != level->_collisionSeparateHandlers.end()) {
				ObjectRef ga = cpShapeGetObject(a);
				ObjectRef gb = cpShapeGetObject(b);
				for (const auto &cb : pos->second) {
					cb(ctp, ga, gb, arb);
				}
			}

			level->onCollisionSeparate(arb);
		}

	}

	Level::collision_type_pair Level::addCollisionMonitor(cpCollisionType a, cpCollisionType b) {
		collision_type_pair ctp(a,b);
		if (_monitoredCollisions.insert(ctp).second) {
			cpSpace * space = getSpace()->getSpace();
			cpCollisionHandler *handler = cpSpaceAddCollisionHandler(space, a, b);
			handler->userData = this;
			handler->beginFunc = detail::Level_collisionBeginHandler;
			handler->preSolveFunc = detail::Level_collisionPreSolveHandler;
			handler->postSolveFunc = detail::Level_collisionPostSolveHandler;
			handler->separateFunc = detail::Level_collisionSeparateHandler;
		}
		return ctp;
	}

	void Level::addCollisionBeginHandler(cpCollisionType a, cpCollisionType b, EarlyCollisionCallback cb) {
		const auto ctp = addCollisionMonitor(a,b);
		_collisionBeginHandlers[ctp].push_back(cb);
	}

	void Level::addCollisionPreSolveHandler(cpCollisionType a, cpCollisionType b, EarlyCollisionCallback cb) {
		const auto ctp = addCollisionMonitor(a,b);
		_collisionPreSolveHandlers[ctp].push_back(cb);
	}

	void Level::addCollisionPostSolveHandler(cpCollisionType a, cpCollisionType b, LateCollisionCallback cb) {
		const auto ctp = addCollisionMonitor(a,b);
		_collisionPostSolveHandlers[ctp].push_back(cb);
	}

	void Level::addCollisionSeparateHandler(cpCollisionType a, cpCollisionType b, LateCollisionCallback cb) {
		const auto ctp = addCollisionMonitor(a,b);
		_collisionSeparateHandlers[ctp].push_back(cb);
	}

	void Level::addContactHandler(cpCollisionType a, cpCollisionType b, ContactCallback cb) {
		const auto ctp = addCollisionMonitor(a,b);
		_contactHandlers[ctp].push_back(cb);
	}

	void Level::setCpBodyVelocityUpdateFunc(cpBodyVelocityFunc f) {
		_bodyVelocityFunc = f ? f : cpBodyUpdateVelocity;

		// update every body in sim
		cpSpaceEachBody(_space, [](cpBody *body, void *data){
			auto l = static_cast<Level*>(data);
			cpBodySetVelocityUpdateFunc(body, l->_bodyVelocityFunc);
		}, this);
	}

	void Level::addedToScenario(ScenarioRef scenario) {
		_scenario = scenario;
	}

	void Level::removeFromScenario() {
		_scenario.reset();
	}

	void Level::onReady() {
		CI_ASSERT_MSG(!_ready, "Can't call onReady() on Level that is already ready");

		const auto self = shared_from_this_as<Level>();
		for (auto &obj : _objects) {
			obj->onReady(self);
		}
		_ready = true;
	}

	void Level::onBodyAddedToSpace(cpBody *body) {
		//CI_LOG_D("onBodyAddedToSpace body: " << body);
		cpBodySetVelocityUpdateFunc(body, getCpBodyVelocityUpdateFunc());
	}

	void Level::onShapeAddedToSpace(cpShape *shape) {
		//CI_LOG_D("onShapeAddedToSpace shape: " << shape);
	}

	void Level::onConstraintAddedToSpace(cpConstraint *constraint) {
		//CI_LOG_D("onConstraintAddedToSpace constraint: " << constraint);
	}

	void Level::dispatchSyntheticContacts() {
		for (auto &group : _syntheticContacts) {
			const auto ctp = group.first;
			const auto pos = _contactHandlers.find(ctp);
			if (pos != _contactHandlers.end()) {
				for (const auto &handler : pos->second) {
					for (auto &objectPair : group.second) {
						handler(ctp, objectPair.first, objectPair.second);
					}
				}
			}
		}
		_syntheticContacts.clear();
	}

	void Level::registerContactBetweenObjects(cpCollisionType a, const ObjectRef &ga, cpCollisionType b, const ObjectRef &gb) {
		collision_type_pair ctp(a, b);
		_syntheticContacts[ctp].push_back(std::make_pair(ga, gb));
	}
	
}
