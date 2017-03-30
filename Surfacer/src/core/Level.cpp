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

#pragma mark - DrawDispatcher

	namespace {

		cpHashValue getCPHashValue(const DrawComponentRef &dc) {
			return reinterpret_cast<cpHashValue>(dc.get());
		}

		cpHashValue getCPHashValue(DrawComponent *dc) {
			return reinterpret_cast<cpHashValue>(dc);
		}

		cpBB gameObjectBBFunc( void *obj )
		{
			return static_cast<DrawComponent*>(obj)->getBB();
		}

		cpCollisionID visibleObjectCollector(void *obj1, void *obj2, cpCollisionID id, void *data) {
			//DrawDispatcher *dispatcher = static_cast<DrawDispatcher*>(obj1);
			DrawComponentRef dc = static_cast<DrawComponent*>(obj2)->shared_from_this<DrawComponent>();
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

			return a->getGameObject()->getId() < b->getGameObject()->getId();
		}
		
	}

	DrawDispatcher::DrawDispatcher():
	_index(cpBBTreeNew( gameObjectBBFunc, NULL ))
	{}

	DrawDispatcher::~DrawDispatcher()
	{
		cpSpatialIndexFree( _index );
	}

	void DrawDispatcher::add( const DrawComponentRef &dc )
	{
		switch( dc->getVisibilityDetermination() )
		{
			case VisibilityDetermination::ALWAYS_DRAW:
				_alwaysVisible.insert( dc );
				break;

			case VisibilityDetermination::FRUSTUM_CULLING:
				cpSpatialIndexInsert( _index, dc.get(), getCPHashValue(dc) );
				break;

			case VisibilityDetermination::NEVER_DRAW:
				break;
		}
	}

	void DrawDispatcher::remove( const DrawComponentRef &dc )
	{
		switch( dc->getVisibilityDetermination() )
		{
			case VisibilityDetermination::ALWAYS_DRAW:
			{
				_alwaysVisible.erase( dc );
				break;
			}

			case VisibilityDetermination::FRUSTUM_CULLING:
			{
				cpSpatialIndexRemove( _index, dc.get(), getCPHashValue(dc) );
				break;
			}

			case VisibilityDetermination::NEVER_DRAW:
				break;
		}

		//
		//	And, just to be safe, remove this object from the current visible set
		//

		_collector.remove(dc);
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

		cpSpatialIndexQuery( _index, this, state.viewport.getFrustum(), visibleObjectCollector, &_collector );

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

	/*
		cpSpace *_space;
		bool _ready, _paused;
		ScenarioWeakRef _scenario;
		set<GameObjectRef> _objects;
		map<size_t,GameObjectRef> _objectsById;
		time_state _time;
		string _name;
		DrawDispatcherRef _drawDispatcher;
	 */

	Level::Level(string name):
	_space(cpSpaceNew()),
	_ready(false),
	_paused(false),
	_time(0,0,0),
	_name(name),
	_drawDispatcher(make_shared<DrawDispatcher>()) {
		// some defaults
		cpSpaceSetIterations( _space, 20 );
		cpSpaceSetDamping( _space, 0.95 );
		cpSpaceSetSleepTimeThreshold( _space, 1 );
	}

	Level::~Level() {
		_objects.clear();
		_objectsById.clear();
		cpSpaceFree(_space);
	}

	void Level::setPaused(bool paused) {
		if (paused != isPaused()) {
			_paused = isPaused();
			if (isPaused()) {
				levelWasPaused(shared_from_this());
			} else {
				levelWasUnpaused(shared_from_this());
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
			cpSpaceStep(_space, time.deltaT);
		}

		if (!_paused) {
			vector<GameObjectRef> moribund;
			for (auto &obj : _objects) {
				if (!obj->isFinished()) {
					obj->step(time);
				} else {
					moribund.push_back(obj);
				}
			}

			if (!moribund.empty()) {
				for (auto &obj : moribund) {
					removeGameObject(obj);
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
			vector<GameObjectRef> moribund;
			for (auto &obj : _objects) {
				if (!obj->isFinished()) {
					obj->update(time);
				} else {
					moribund.push_back(obj);
				}
			}

			if (!moribund.empty()) {
				for (auto &obj : moribund) {
					removeGameObject(obj);
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

	void Level::addGameObject(GameObjectRef obj) {
		CI_ASSERT_MSG(!obj->getLevel(), "Can't add a GameObject that already has been added to this or another Level");

		_objects.insert(obj);
		_objectsById[obj->getId()] = obj;

		if (DrawComponentRef dc = obj->getDrawComponent()) {
			_drawDispatcher->add(dc);
		}

		obj->onAddedToLevel(shared_from_this());

		if (_ready) {
			obj->onReady(shared_from_this());
		}

	}

	void Level::removeGameObject(GameObjectRef obj) {
		CI_ASSERT_MSG(obj->getLevel().get() == this, "Can't remove a GameObject which isn't a child of this Level");

		_objects.erase(obj);
		_objectsById.erase(obj->getId());

		if (DrawComponentRef dc = obj->getDrawComponent()) {
			_drawDispatcher->remove(dc);
		}

		obj->onRemovedFromLevel();
	}

	GameObjectRef Level::getGameObjectById(size_t id) const {
		auto pos = _objectsById.find(id);
		if (pos != _objectsById.end()) {
			return pos->second;
		} else {
			return nullptr;
		}
	}

	vector<GameObjectRef> Level::getGameObjectsByName(string name) const {

		vector<GameObjectRef> result;
		copy_if(_objects.begin(), _objects.end(), back_inserter(result),[&name](const GameObjectRef &gameObject) -> bool {
			return gameObject->getName() == name;
		});

		return result;
	}

	const Viewport &Level::camera() const {
		return getScenario()->getCamera();
	}

	Viewport &Level::camera() {
		return getScenario()->getCamera();
	}

	void Level::addedToScenario(ScenarioRef scenario) {
		_scenario = scenario;
	}

	void Level::removeFromScenario() {
		_scenario.reset();
	}

	void Level::onReady() {
		CI_ASSERT_MSG(!_ready, "Can't call onReady() on Level that is already ready");

		const auto self = shared_from_this();
		for (auto &obj : _objects) {
			obj->onReady(self);
		}
		_ready = true;
	}


}