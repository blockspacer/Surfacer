//
//  System.cpp
//  Surfacer
//
//  Created by Shamyl Zakariya on 10/23/17.
//

#include "ParticleSystem.hpp"

#include <chipmunk/chipmunk_unsafe.h>

using namespace core;
namespace particles {
	
	namespace {
		
		double MinKinematicParticleRadius = 0.05;
		
	}
	
#pragma mark - ParticleSimulation

	/*
	 size_t _count;
	 cpBB _bb;
	 vector<particle_template> _templates;
	 core::SpaceAccessRef _spaceAccess;
	 */
		
	ParticleSimulation::ParticleSimulation():
	BaseParticleSimulation(),
	_count(0),
	_bb(cpBBInvalid)
	{}
	
	// Component
	void ParticleSimulation::onReady(core::ObjectRef parent, core::StageRef stage) {
		BaseParticleSimulation::onReady(parent, stage);
		_spaceAccess = stage->getSpace();
	}

	void ParticleSimulation::onCleanup() {
		BaseParticleSimulation::onCleanup();
	}

	void ParticleSimulation::update(const core::time_state &time) {
		BaseParticleSimulation::update(time);
		_prepareForSimulation(time);
		_simulate(time);
	}
	
	// BaseParticleSimulation
	
	void ParticleSimulation::setParticleCount(size_t count) {
		BaseParticleSimulation::setParticleCount(count);
		_templates.resize(count);
	}
	
	size_t ParticleSimulation::getFirstActive() const {
		// ALWAYS zero
		return 0;
	}

	size_t ParticleSimulation::getActiveCount() const {
		return min(_count, _state.size());
	}

	cpBB ParticleSimulation::getBB() const {
		return _bb;
	}
	
	// ParticleSimulation
	
	void ParticleSimulation::emit(const particle_template &particle) {
		_pending.push_back(particle);
	}
	
	void ParticleSimulation::emit(const particle_template &particle, const dvec2 &world, const dvec2 &vel) {
		_pending.push_back(particle);
		_pending.back().position = world;
		_pending.back().velocity = vel;
	}
	
	void ParticleSimulation::_prepareForSimulation(const core::time_state &time) {
		// run a first pass where we update age and completion, then if necessary perform a compaction pass
		size_t expiredCount = 0;
		const size_t activeCount = getActiveCount();
		
		auto state = _state.begin();
		auto end = state + activeCount;
		auto templ = _templates.begin();
		for (; state != end; ++state, ++templ) {
			
			//
			// update age and completion
			//
			
			templ->_age += time.deltaT;
			templ->_completion = templ->_age / templ->lifespan;
			
			if (templ->_completion > 1) {
				
				//
				// This particle is expired. Clean it up, and note how many expired we have
				//
				
				templ->destroy();
				expiredCount++;
			}
		}
		
		if (expiredCount > activeCount / 2) {
			
			//
			// parition templates such that expired ones are at the end; then reset _count accordingly
			// note: We don't need to sort _particleState because it's ephemeral; we'll overwrite what's
			// needed next pass to update()
			//
			
			auto end = partition(_templates.begin(), _templates.begin() + activeCount, [](const particle_template &templ){
				return templ._completion <= 1;
			});
			
			_count = end - _templates.begin();
			
			CI_LOG_D("COMPACTED, went from: " << activeCount << " to " << getActiveCount() << " active particles");
		}
		
		//
		//	Process any particles that were emitted
		//

		if (!_pending.empty()) {
		
			for (auto &particle : _pending) {

				//
				// if a particle already lives at this point, perform any cleanup needed
				//
				
				const size_t idx = _count % _templates.size();
				_templates[idx].destroy();
				
				//
				//	Assign template, and if it's kinematic, create chipmunk physics backing
				//
				
				_templates[idx] = particle;
				
				if (particle.kinematics) {
					double mass = particle.mass.getInitialValue();
					double radius = max(particle.radius.getInitialValue(), MinKinematicParticleRadius);
					double moment = cpMomentForCircle(mass, 0, radius, cpvzero);
					cpBody *body = cpBodyNew(mass, moment);
					cpShape *shape = cpCircleShapeNew(body, radius, cpvzero);
					
					// set up user data, etc to play well with our "engine"
					cpBodySetUserData(body, this);
					cpShapeSetUserData(shape, this);
					_spaceAccess->addBody(body);
					_spaceAccess->addShape(shape);
					
					// set initial state
					cpBodySetPosition(body, cpv(particle.position));
					cpBodySetVelocity(body, cpv(particle.velocity));
					cpShapeSetFilter(shape, particle.kinematics.filter);
					cpShapeSetFriction(shape, particle.kinematics.friction);
					
					_templates[idx]._body = body;
					_templates[idx]._shape = shape;
					_templates[idx].position = particle.position;
					_templates[idx].velocity = particle.velocity;
				}
				
				_templates[idx].prepare();
				
				_count++;
			}
			
			_pending.clear();
		}
		
		// we'll re-enable in _simulate
		// TODO: Consider a more graceful handling of this, since re-enablement below is awkward
		for (auto &state : _state) {
			state.active = false;
		}
	}

	void ParticleSimulation::_simulate(const core::time_state &time) {
		
		const auto &gravities = getStage()->getGravities();
		cpBB bb = cpBBInvalid;
		
		auto state = _state.begin();
		auto end = state + getActiveCount();
		auto templ = _templates.begin();
		size_t idx = 0;
		for (; state != end; ++state, ++templ, ++idx) {
			
			// _prepareForSimulation deactivates all particles
			// and requires _simulate to re-activate any which should be active

			state->active = templ->_completion <= 1;
			if (!state->active) {
				continue;
			}

			const auto radius = templ->radius(templ->_completion);
			const auto size = radius * M_SQRT2;
			const auto damping = 1-saturate(templ->damping(templ->_completion));
			const auto additivity = templ->additivity(templ->_completion);
			const auto mass = templ->mass(templ->_completion);
			const auto color = templ->color(templ->_completion);
			
			bool didRotate = false;
			
			if (!templ->kinematics) {
				
				//
				//	Non-kinematic particles get standard velocity + damping + gravity applied
				//
				
				templ->position = templ->position + templ->velocity * time.deltaT;
				
				for (const auto &gravity : gravities) {
					auto force = gravity->calculate(templ->position);
					templ->velocity += mass * force.magnitude * force.dir * time.deltaT;
				}
				
				if (damping < 1) {
					templ->velocity *= damping;
				}
				
				
			} else {
				
				//
				// Kinematic bodies are simulated by chipmunk; extract position, rotation etc
				//
				
				cpShape *shape = templ->_shape;
				cpBody *body = templ->_body;
				
				templ->position = v2(cpBodyGetPosition(body));
				
				if (damping < 1) {
					cpBodySetVelocity(body, cpvmult(cpBodyGetVelocity(body), damping));
					cpBodySetAngularVelocity(body, damping * cpBodyGetAngularVelocity(body));
				}
				
				templ->velocity = v2(cpBodyGetVelocity(body));
				
				if (!templ->orientToVelocity) {
					dvec2 rotation = v2(cpBodyGetRotation(body));
					state->right = rotation * size;
					state->up = rotateCCW(state->right);
					didRotate = true;
				}
				
				// now update chipmunk's representation
				cpBodySetMass(body, max(mass, 0.0));
				cpCircleShapeSetRadius(shape, max(radius,MinKinematicParticleRadius));
			}
			
			if (templ->orientToVelocity) {
				dvec2 dir = templ->velocity;
				double len2 = lengthSquared(dir);
				if (templ->orientToVelocity && len2 > 1e-2) {
					state->right = (dir/sqrt(len2)) * size;
					state->up = rotateCCW(state->right);
					didRotate = true;
				}
			}
			
			if (!didRotate) {
				// default rotation
				state->right.x = size;
				state->right.y = 0;
				state->up.x = 0;
				state->up.y = size;
			}
			
			state->atlasIdx = templ->atlasIdx;
			state->age = templ->_age;
			state->completion = templ->_completion;
			state->position = templ->position;
			state->color = color;
			state->additivity = additivity;
			
			bb = cpBBExpand(bb, templ->position, size);
		}
		
		//
		// update BB and notify
		//
		
		_bb = bb;
		notifyMoved();
	}
	
#pragma mark - ParticleEmitter
	
	/*
	 ParticleSimulationWeakRef _simulation;
	 vector<emission> _emissions;
	 vector<particle_templates> _templates;
	 vector<int> _templateLookup;
	*/
	
	ParticleEmitter::ParticleEmitter(uint32_t seed):
	_rng(seed)
	{}
	
	void ParticleEmitter::update(const core::time_state &time) {
		if (ParticleSimulationRef sim = _simulation.lock()) {

		}
	}
	
	void ParticleEmitter::onReady(ObjectRef parent, StageRef stage) {
		if (!getSimulation()) {
			ParticleSimulationRef sim = getSibling<ParticleSimulation>();
			if (sim) {
				setSimulation(sim);
			}
		}
	}
	
	// ParticleEmitter
	
	void ParticleEmitter::setSimulation(const ParticleSimulationRef simulation) {
		_simulation = simulation;
	}
	
	ParticleSimulationRef ParticleEmitter::getSimulation() const {
		return _simulation.lock();
	}
	
	void ParticleEmitter::seed(uint32_t seed) {
		_rng.seed(seed);
	}
	
	void ParticleEmitter::add(ParticleSimulation::particle_template templ, float variance, int probability) {
		size_t idx = _templates.size();
		_templates.push_back({templ, variance});
		for (int i = 0; i < probability; i++) {
			_templateLookup.push_back(idx);
		}
	}
	
	void ParticleEmitter::emit(dvec2 world, double radius, dvec2 vel, seconds_t duration, double rate, Envelope env) {
		
	}
	
	void ParticleEmitter::emit(dvec2 world, double radius, dvec2 vel, int count) {
		if (ParticleSimulationRef sim = _simulation.lock()) {
			for (int i = 0; i < count; i++) {
				size_t idx = static_cast<size_t>(_rng.nextUint()) % _templateLookup.size();
				const particle_templates &templs = _templates[_templateLookup[idx]];
				dvec2 pos = world + radius * static_cast<double>(_rng.nextFloat()) * dvec2(_rng.nextVec2());
				dvec2 velocity = perturb(templs.templ.velocity, templs.variance) + perturb(vel, templs.variance);
				
				sim->emit(templs.templ, pos, velocity);
			}
		}
	}
	
	double ParticleEmitter::nextDouble(double variance) {
		return 1.0 + static_cast<double>(_rng.nextFloat(-variance, variance));
	}

	dvec2 ParticleEmitter::nextDVec2(double variance) {
		return nextDouble(variance) * dvec2(_rng.nextVec2());
	}
	
	dvec2 ParticleEmitter::perturb(const dvec2 dir, double variance) {
		double len2 = lengthSquared(dir);
		if (len2 > 0) {
			double len = sqrt(len2);
			dvec2 p(_rng.nextFloat(-len, +len), _rng.nextFloat(-len,+len));
			return dir + nextDouble(variance) * p;
		}
		return dir;
	}


#pragma mark - ParticleSystemDrawComponent
	
	/*
	 config _config;
	 gl::GlslProgRef _shader;
	 vector<particle_vertex> _particles;
	 gl::VboRef _particlesVbo;
	 gl::BatchRef _particlesBatch;
	 GLsizei _batchDrawStart, _batchDrawCount;
	 */
	
	ParticleSystemDrawComponent::config ParticleSystemDrawComponent::config::parse(const util::xml::XmlMultiTree &node) {
		config c = BaseParticleSystemDrawComponent::config::parse(node);
		
		auto textureName = node.getAttribute("textureAtlas");
		if (textureName) {
			
			auto image = loadImage(app::loadAsset(*textureName));
			gl::Texture2d::Format fmt = gl::Texture2d::Format().mipmap(false);
			
			c.textureAtlas = gl::Texture2d::create(image, fmt);
			c.atlasType = Atlas::fromString(node.getAttribute("atlasType", "None"));
		}
		
		return c;
	}
	
	ParticleSystemDrawComponent::ParticleSystemDrawComponent(config c):
	BaseParticleSystemDrawComponent(c),
	_config(c),
	_batchDrawStart(0),
	_batchDrawCount(0)
	{
		auto vsh = CI_GLSL(150,
						   uniform mat4 ciModelViewProjection;
						   
						   in vec4 ciPosition;
						   in vec2 ciTexCoord0;
						   in vec4 ciColor;
						   
						   out vec2 TexCoord;
						   out vec4 Color;
						   
						   void main(void){
							   gl_Position = ciModelViewProjection * ciPosition;
							   TexCoord = ciTexCoord0;
							   Color = ciColor;
						   }
						   );
		
		auto fsh = CI_GLSL(150,
						   uniform sampler2D uTex0;
						   
						   in vec2 TexCoord;
						   in vec4 Color;
						   
						   out vec4 oColor;
						   
						   void main(void) {
							   float alpha = round(texture( uTex0, TexCoord ).r);
							   
							   // NOTE: additive blending requires premultiplication
							   oColor.rgb = Color.rgb * alpha;
							   oColor.a = Color.a * alpha;
						   }
						   );
		
		_shader = gl::GlslProg::create(gl::GlslProg::Format().vertex(vsh).fragment(fsh));
	}
	
	void ParticleSystemDrawComponent::setSimulation(const BaseParticleSimulationRef simulation) {
		BaseParticleSystemDrawComponent::setSimulation(simulation);
		
		// now build our GPU backing. Note, GL_QUADS is deprecated so we need GL_TRIANGLES, which requires 6 vertices to make a quad
		size_t count = simulation->getParticleCount();
		_particles.resize(count * 6, particle_vertex());
		
		updateParticles(simulation);
		
		// create VBO GPU-side which we can stream to
		_particlesVbo = gl::Vbo::create( GL_ARRAY_BUFFER, _particles, GL_STREAM_DRAW );
		
		geom::BufferLayout particleLayout;
		particleLayout.append( geom::Attrib::POSITION, 2, sizeof( particle_vertex ), offsetof( particle_vertex, position ) );
		particleLayout.append( geom::Attrib::TEX_COORD_0, 2, sizeof( particle_vertex ), offsetof( particle_vertex, texCoord ) );
		particleLayout.append( geom::Attrib::COLOR, 4, sizeof( particle_vertex ), offsetof( particle_vertex, color ) );
		
		// pair our layout with vbo.
		auto mesh = gl::VboMesh::create( static_cast<uint32_t>(_particles.size()), GL_TRIANGLES, { { particleLayout, _particlesVbo } } );
		_particlesBatch = gl::Batch::create( mesh, _shader );
	}
	
	void ParticleSystemDrawComponent::draw(const render_state &renderState) {
		auto sim = getSimulation();
		if (updateParticles(sim)) {
			gl::ScopedTextureBind tex(_config.textureAtlas, 0);
			gl::ScopedBlendPremult blender;
			
			_particlesBatch->draw(_batchDrawStart, _batchDrawCount);
			
			if (renderState.mode == RenderMode::DEVELOPMENT) {
				// draw BB
				cpBB bb = getBB();
				const ColorA bbColor(1,0.2,1,0.5);
				gl::color(bbColor);
				gl::drawStrokedRect(Rectf(bb.l, bb.b, bb.r, bb.t), 1);
			}
		}
	}
	
	bool ParticleSystemDrawComponent::updateParticles(const BaseParticleSimulationRef &sim) {
		
		// walk the simulation particle state and write to our vertices		
		const Atlas::Type AtlasType = _config.atlasType;
		const vec2* AtlasOffsets = Atlas::AtlasOffsets(AtlasType);
		const float AtlasScaling = Atlas::AtlasScaling(AtlasType);
		const ci::ColorA transparent(0,0,0,0);
		const vec2 origin(0,0);
		
		vec2 shape[4];
		mat2 rotator;
		const size_t activeCount = sim->getActiveCount();
		auto vertex = _particles.begin();
		auto stateBegin = sim->getParticleState().begin();
		auto stateEnd = stateBegin + activeCount;
		int verticesWritten = 0;
		
		for (auto state = stateBegin; state != stateEnd; ++state) {
			
			// Check if particle is active && visible before writing geometry
			if (state->active && state->color.a >= ALPHA_EPSILON) {
				
				shape[0] = state->position - state->right + state->up;
				shape[1] = state->position + state->right + state->up;
				shape[2] = state->position + state->right - state->up;
				shape[3] = state->position - state->right - state->up;
				
				//
				//	For each vertex, assign position, color and texture coordinate
				//	Note, GL_QUADS is deprecated so we have to draw two TRIANGLES
				//
				
				ci::ColorA pc = state->color;
				ci::ColorA additiveColor( pc.r * pc.a, pc.g * pc.a, pc.b * pc.a, pc.a * ( 1 - static_cast<float>(state->additivity)));
				vec2 atlasOffset = AtlasOffsets[state->atlasIdx];
				
				// GL_TRIANGLE
				vertex->position = shape[0];
				vertex->texCoord = (TexCoords[0] * AtlasScaling) + atlasOffset;
				vertex->color = additiveColor;
				++vertex;
				
				vertex->position = shape[1];
				vertex->texCoord = (TexCoords[1] * AtlasScaling) + atlasOffset;
				vertex->color = additiveColor;
				++vertex;
				
				vertex->position = shape[2];
				vertex->texCoord = (TexCoords[2] * AtlasScaling) + atlasOffset;
				vertex->color = additiveColor;
				++vertex;
				
				// GL_TRIANGLE
				vertex->position = shape[0];
				vertex->texCoord = (TexCoords[0] * AtlasScaling) + atlasOffset;
				vertex->color = additiveColor;
				++vertex;
				
				vertex->position = shape[2];
				vertex->texCoord = (TexCoords[2] * AtlasScaling) + atlasOffset;
				vertex->color = additiveColor;
				++vertex;
				
				vertex->position = shape[3];
				vertex->texCoord = (TexCoords[3] * AtlasScaling) + atlasOffset;
				vertex->color = additiveColor;
				++vertex;
				
				verticesWritten += 6;

			} else {
				//
				// active == false or alpha == 0, so this particle isn't visible:
				// write 2 triangles which will not be rendered
				//
				for (int i = 0; i < 6; i++) {
					vertex->position = origin;
					vertex->color = transparent;
					++vertex;
				}
			}
		}
		
		if (_particlesVbo && verticesWritten > 0) {
			
			// transfer to GPU
			_batchDrawStart = 0;
			_batchDrawCount = static_cast<GLsizei>(activeCount) * 6;
			void *gpuMem = _particlesVbo->mapReplace();
			memcpy( gpuMem, _particles.data(), _batchDrawCount * sizeof(particle_vertex) );
			_particlesVbo->unmap();
			
			return true;
		}
		
		return false;
	}


#pragma mark - System
	
	ParticleSystem::config ParticleSystem::config::parse(const util::xml::XmlMultiTree &node) {
		config c;
		c.maxParticleCount = util::xml::readNumericAttribute<size_t>(node, "count", c.maxParticleCount);
		c.drawConfig = ParticleSystemDrawComponent::config::parse(node.getChild("draw"));
		return c;
	}
	
	ParticleSystemRef ParticleSystem::create(string name, const config &c) {
		auto simulation = make_shared<ParticleSimulation>();
		simulation->setParticleCount(c.maxParticleCount);
		auto draw = make_shared<ParticleSystemDrawComponent>(c.drawConfig);
		return Object::create<ParticleSystem>(name, { draw, simulation });
	}
	
	ParticleSystem::ParticleSystem(string name):
	BaseParticleSystem(name)
	{}
	
	ParticleEmitterRef ParticleSystem::createEmitter() {
		ParticleEmitterRef emitter = make_shared<ParticleEmitter>();
		emitter->setSimulation(getComponent<ParticleSimulation>());
		addComponent(emitter);
		return emitter;
	}
	
	
}
