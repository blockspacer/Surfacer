//
//  Svg.cpp
//  Surfacer
//
//  Created by Shamyl Zakariya on 4/4/17.
//
//

#include "Svg.hpp"

#include "ChipmunkHelpers.hpp"
#include "SvgParsing.hpp"
#include "Strings.hpp"

namespace core { namespace util { namespace svg {

#pragma mark - Parsing

	namespace {

		string indent( int l ) {
			return string( l, '\t' );
		}

		template< typename TRANSFORMER >
		cpBB transform(const ci::TriMeshRef &sourceMesh, ci::TriMeshRef &destMesh,
					   const vector<Shape::stroke> &sourceStrokes, vector<Shape::stroke> &destStrokes,
					   const TRANSFORMER &transformer )
		{
			//
			//	transform vertices from sourceMesh into destMesh, copying over same indices
			//

			const vec2 *vertices = sourceMesh->getPositions<2>();
			const size_t numVertices = sourceMesh->getNumVertices();
			const vector<uint32_t> &indices = sourceMesh->getIndices();

			cpBB bounds = cpBBInvalid;
			vector<vec2> destVertices;
			destVertices.reserve(numVertices);
			for( int i = 0; i < numVertices; i++ ) {
				const vec2 v = transformer(vertices[i]);
				cpBBExpand(bounds, v);
				destVertices.push_back(v);
			}

			vector< uint32_t > destIndices;
			destIndices.reserve(indices.size());
			for( auto index : indices ) {
				destIndices.push_back(index);
			}

			//
			//	transfer new vertices and same indices over to destMesh
			//

			if (destMesh) {
				destMesh->clear();
			} else {
				destMesh = make_shared<TriMesh>(TriMesh::Format().positions(2));
			}

			destMesh->appendPositions( &(destVertices.front()), destVertices.size() );
			destMesh->appendIndices( &(destIndices.front()), destIndices.size() );

			//
			//	transform vertices in sourceStrokes into destStrokes
			//

			destStrokes.clear();
			for( auto stroke : sourceStrokes) {
				destStrokes.emplace_back();
				Shape::stroke &back( destStrokes.back());

				back.closed = stroke.closed;
				for(auto v : stroke.vertices) {
					back.vertices.push_back( transformer(v) );
				}
			}

			return bounds;
		}

		struct to_world_transformer {
			double documentScale;
			dvec2 documentSize;
			dmat4 toWorld;

			to_world_transformer( const dvec2 &dSize, double dScale, const dmat4 tw ):
			documentScale( dScale ),
			documentSize( dSize ),
			toWorld( tw )
			{}

			inline dvec2 operator()( const dvec2 &v ) const {
				dvec2 world = ( toWorld * v ) * documentScale;
				world.y = documentSize.y - world.y;

				return world;
			}
		};

		struct to_local_transformer {
			dvec2 localOrigin;
			to_local_transformer( const dvec2 &lo ):
			localOrigin(lo)
			{}

			inline dvec2 operator()( const dvec2 &v ) const {
				return v - localOrigin;
			}
		};

	} // end anonymous namespace

#pragma mark - Shape::stroke

	Shape::stroke::~stroke() {
		if (vao) {
			glDeleteBuffers(1, &vao);
			glDeleteBuffers(1, &vbo);
		}
	}

#pragma mark - Shape

	/*
		bool _origin, _filled, _stroked;
		dmat4 _svgTransform;
		ci::ColorA _fillColor, _strokeColor;
		double _strokeWidth;
		map< string, string > _attributes;
		string _type, _id;
		ci::Triangulator::Winding _fillRule;
		ci::TriMeshRef _svgMesh, _worldMesh, _localMesh;
		ci::gl::VboMeshRef _localVboMesh;

		vector<stroke> _svgStrokes, _worldStrokes, _localStrokes;
		ci::Rectd _worldBounds, _localBounds;

		BlendMode _blendMode;
	 */

	Shape::Shape():
	_origin(false),
	_filled(false),
	_stroked(false),
	_fillColor(0,0,0,1),
	_strokeColor(0,0,0,1),
	_strokeWidth(0),
	_fillRule( Triangulator::WINDING_NONZERO )
	{}

	Shape::~Shape() {}

	void Shape::parse( const ci::XmlTree &shapeNode )
	{
		_type = shapeNode.getTag();

		if ( shapeNode.hasAttribute( "id" )) {
			_id = shapeNode.getAttribute( "id" ).getValue();
		}

		//
		//	Cache all attributes, in case we want to query them later
		//

		for( const XmlTree::Attr &attr : shapeNode.getAttributes() ) {
			_attributes[ attr.getName() ] = attr.getValue();
		}

		//
		//	Parse style declarations
		//

		{
			util::svg::svg_style style = util::svg::parseStyle( shapeNode );

			_filled = style.isFilled();
			_stroked = style.isStroked();
			_fillColor = ColorA( style.fillColor, style.fillOpacity * style.opacity );
			_strokeColor = ColorA( style.strokeColor, style.strokeOpacity * style.opacity );
			_strokeWidth = style.strokeWidth;
			_fillRule = style.fillRule;

			//
			//	Sanity check
			//

			if (!_filled && !_stroked) {
				_filled = true;
			}
		}

		//
		//	Get the transform
		//

		if ( shapeNode.hasAttribute( "transform" )) {
			_svgTransform = util::svg::parseTransform( shapeNode.getAttribute( "transform" ).getValue() );
		}

		//
		//	determine if this shape is the origin shape for its parent Group
		//

		if ( shapeNode.hasChild( "title" )) {
			string title = shapeNode.getChild( "title" ).getValue();
			title = strings::strip( strings::lowercase( title ));
			if ( title == "__origin__" ) _origin = true;
		}

		if ( shapeNode.hasAttribute( "__origin__" )) {
			_origin = true;
		}

		if ( shapeNode.hasAttribute( "class" )) {
			string classes = strings::lowercase(shapeNode.getAttribute( "class" ).getValue());
			if ( classes.find( "__origin__" ) != string::npos ) {
				_origin = true;
			}
		}

		//
		// any name starting with "origin" makes it an origin node
		//

		if ( _id.find( "__origin__" ) == 0 ) {
			_origin = true;
		}

		//
		//	parse svg shape into a ci::Shape2d, and then convert that to _svgMesh in document space.
		//	later, in Group::_normalize we'll project to world space, and then to local space.
		//

		Shape2d shape2d;
		util::svg::parseShape( shapeNode, shape2d );
		_build( shape2d );
	}

	void Shape::draw( const render_state &state, const GroupRef &owner, double opacity, const ci::gl::GlslProgRef &shader ) {
		switch( state.mode ) {
			case RenderMode::GAME:
				if ( !isOrigin() ) {
					_drawGame(state, owner, opacity, _localMesh, _localStrokes, shader );
				}
				break;

			case RenderMode::DEVELOPMENT:
				_drawDebug(state, owner, opacity, _localMesh, _localStrokes, shader );
				break;

			case RenderMode::COUNT:
				break;
		}
	}

	void Shape::_build( const ci::Shape2d &shape ) {
		_svgMesh.reset();
		_worldMesh.reset();
		_localMesh.reset();
		_svgStrokes.clear();
		_worldStrokes.clear();
		_localStrokes.clear();

		const double ApproximationScale = 0.5;

		//
		//	we only need to triangulate filled shapes
		//

		if ( isFilled() ) {
			_svgMesh = Triangulator(shape, ApproximationScale).createMesh(_fillRule);
		}

		//
		// we only need to gather the perimeter vertices if our shape is stroked
		//

		if ( isStroked() ) {
			for( const Path2d &path : shape.getContours() ) {
				_svgStrokes.push_back( stroke() );
				_svgStrokes.back().closed = path.isClosed();
				_svgStrokes.back().vertices = path.subdivide( ApproximationScale );
			}
		}
	}

	Rectd Shape::_projectToWorld( const dvec2 &documentSize, double documentScale, const dmat4 &worldTransform ) {
		dmat4 toWorld = worldTransform * _svgTransform;
		cpBB bounds = transform( _svgMesh, _worldMesh, _svgStrokes, _worldStrokes, to_world_transformer( documentSize, documentScale, toWorld ));

		_worldBounds.set(bounds.l, bounds.b, bounds.r, bounds.t);
		return _worldBounds;
	}

	void Shape::_makeLocal( const dvec2 &originWorld ) {
		transform( _worldMesh, _localMesh, _worldStrokes, _localStrokes, to_local_transformer( originWorld ));

		// free up memory we're not using anymore
		_worldMesh.reset();
		_svgMesh.reset();
		_worldStrokes.clear();
		_svgStrokes.clear();
	}

	void Shape::_drawDebug( const render_state &state, const GroupRef &owner, double opacity, const ci::TriMeshRef &mesh, vector<stroke> &strokes, const ci::gl::GlslProgRef &shader ) {
		if ( isOrigin() ) {
			dmat4 worldTransform = owner->_worldTransform(dmat4(1));

			double screenSize = 20;
			dvec2 xAxis( worldTransform[0]);
			dvec2 yAxis( worldTransform[1]);

			dvec2 scale( screenSize / length(xAxis), screenSize / length(yAxis));
			scale *= state.viewport->getReciprocalScale();

			gl::color(1,0,0);
			gl::drawLine( vec2(0,0), vec2(scale.x,0));
			gl::color(0.25,0,0);
			gl::drawLine( vec2(0,0), vec2(-scale.x,0));

			gl::color(0,1,0);
			gl::drawLine( vec2(0,0), vec2(0,scale.y));
			gl::color(0,0.25,0);
			gl::drawLine( vec2(0,0), vec2(0,-scale.y));
		}
		else {

			//
			//	Crude immediate-mode style drawing of meshes and strokes
			//

			if ( isFilled() )
			{
				ci::ColorA fc = getFillColor();
				shader->uniform("Color", ColorA(fc.r, fc.g, fc.b, fc.a * opacity));
				gl::draw(*mesh);
			}

			if (isStroked()) {
				ci::ColorA sc = getStrokeColor();
				shader->uniform("Color", ColorA(sc.r, sc.g, sc.b, sc.a * opacity));
				gl::lineWidth(1);

				for( auto &stroke : strokes )
				{
					vec2 a = stroke.vertices.front();
					for (auto it = stroke.vertices.begin()+1, end = stroke.vertices.end(); it != end; ++it) {
						vec2 b = *it;
						gl::drawLine(a, b);
						a = b;
					}

					if (stroke.closed) {
						gl::drawLine(a, stroke.vertices.front());
					}
				}
			}
		}
	}

	void Shape::_drawGame( const render_state &state, const GroupRef &owner, double opacity, const ci::TriMeshRef &mesh, vector<stroke> &strokes, const ci::gl::GlslProgRef &shader ) {
		gl::Context::getCurrent()->setDefaultShaderVars();
		_drawFills(state, owner, opacity, mesh, shader);
		_drawStrokes(state, owner, opacity, strokes, shader);
	}

	void Shape::_drawStrokes( const render_state &state, const GroupRef &owner, double opacity, vector<stroke> &strokes, const ci::gl::GlslProgRef &shader ) {
		if (isStroked()) {

			ci::ColorA sc = getStrokeColor();
			shader->uniform("Color", ColorA(sc.r, sc.g, sc.b, sc.a * opacity));
			gl::lineWidth(1);

			for( auto &stroke : strokes )
			{
				if (!stroke.vboMesh) {
					gl::VboMesh::Layout layout;
					layout.usage( GL_STATIC_DRAW ).attrib( geom::POSITION, 2 );

					const GLsizei count = static_cast<GLsizei>(stroke.vertices.size());
					const GLenum primitive = stroke.closed ? GL_LINE_LOOP : GL_LINE_STRIP;
					stroke.vboMesh = gl::VboMesh::create(count,primitive,{ layout });
					stroke.vboMesh->bufferAttrib( geom::POSITION, count * sizeof( vec2 ), stroke.vertices.data() );
				}

				gl::draw(stroke.vboMesh);
			}
		}
	}

	void Shape::_drawFills( const render_state &state, const GroupRef &owner, double opacity, const ci::TriMeshRef &mesh, const ci::gl::GlslProgRef &shader ) {
		if ( isFilled() )
		{
			ci::ColorA fc = getFillColor();
			shader->uniform("Color", ColorA(fc.r, fc.g, fc.b, fc.a * opacity));

			if (!_localVboMesh) {
				_localVboMesh = gl::VboMesh::create(*mesh);
			}

			gl::draw(_localVboMesh);
		}
	}


#pragma mark - Group

	/*
	 GroupWeakRef _parent;
	 ShapeRef _originShape;

	 std::string _id;
	 double _opacity;
	 bool _transformDirty;
	 double _localTransformAngle;
	 dvec2 _localTransformPosition, _localTransformScale, _documentSize;
	 dmat4 _groupTransform, _transform;
	 BlendMode _blendMode;

	 std::vector< ShapeRef > _shapes;
	 std::map< std::string, ShapeRef > _shapesByName;
	 std::vector< GroupRef > _groups;
	 std::map< std::string, GroupRef > _groupsByName;
	 std::map< std::string, std::string > _attributes;
	 std::vector< drawable > _drawables;
	 */

	Group::Group():
	_opacity(1),
	_transformDirty(true),
	_localTransformAngle(0),
	_localTransformPosition(0,0),
	_localTransformScale(1,1),
	_documentSize(0,0)
	{}

	Group::~Group() {
		clear();
	}

	void Group::load( ci::DataSourceRef svgData, double documentScale) {
		clear();

		//
		//	We're reading a document, not a <g> group; but we want to load attributes and children and (if any,) shapes
		//

		XmlTree svgNode = XmlTree( svgData ).getChild( "svg" );

		parse( svgNode );

		//
		//	Figure out the document size. This is important because we use a bottom-left
		//	coordinate system, and SVG uses a top-left. We need to flip.
		//

		const Rectd documentFrame = util::svg::parseDocumentFrame(svgNode);
		const dvec2 parentWorldOrigin(documentFrame.getX1(),documentFrame.getY1());
		_documentSize.x = documentFrame.getWidth();
		_documentSize.y = documentFrame.getHeight();

		//
		//	Now normalize our generated geometry
		//

		_normalize( _documentSize, documentScale, dmat4(1), parentWorldOrigin );
	}

	void Group::clear() {
		_originShape.reset();
		_shapes.clear();
		_shapesByName.clear();
		_groups.clear();
		_groupsByName.clear();
		_attributes.clear();
		_drawables.clear();
	}

	void Group::parse( const ci::XmlTree &svgGroupNode ) {
		clear();
		_parseGroupAttributes( svgGroupNode );
		_loadGroupsAndShapes( svgGroupNode );
	}

	void Group::draw( const render_state &state ) {

		//
		//	Build the shader (lazily)
		//

		if (isRoot() && !_shader) {
			auto vsh = CI_GLSL(150,
							   uniform mat4 ciModelViewProjection;
							   in vec4 ciPosition;

							   void main(void){
								   gl_Position = ciModelViewProjection * ciPosition;
							   }
							   );

			auto fsh = CI_GLSL(150,
							   uniform vec4 Color;
							   out vec4 oColor;

							   void main(void) {
								   oColor = Color;
							   }
							   );


			_shader = gl::GlslProg::create(gl::GlslProg::Format().vertex(vsh).fragment(fsh));
		}

		gl::ScopedGlslProg sp(_shader);
		_draw( state, 1, _shader );
	}

	void Group::trace( int depth ) const {
		std::string
		ind = indent(depth),
		ind2 = indent( depth+1 );

		app::console() << ind << "[Group name: " << getId() << std::endl << ind << " +shapes:" << std::endl;

		for( ShapeRef shape : getShapes() ) {
			app::console() << ind2 << "[SvgShape name: " << shape->getId()
			<< " type: " << shape->getType()
			<< " origin: " << str(shape->isOrigin())
			<< " filled: " << str(shape->isFilled()) << " stroked: " << str(shape->isStroked())
			<< " fillColor: " << shape->getFillColor()
			<< " strokeColor: " << shape->getStrokeColor()
			<< "]" << std::endl;
		}

		app::console() << ind << " +groups:" << std::endl;
		for( const GroupRef &obj : getGroups() ) {
			obj->trace( depth + 1 );
		}

		app::console() << ind << "]" << std::endl;
	}

	GroupRef Group::getRoot() const {
		auto r = const_cast<Group*>(this)->shared_from_this();

		while( GroupRef p = r->getParent() ) {
			r = p;
		}

		return r;
	}

	bool Group::isRoot() const {
		return !getParent();
	}

	GroupRef Group::getGroupNamed( const std::string &name ) const {
		auto pos( _groupsByName.find( name ));
		if ( pos != _groupsByName.end() ) return pos->second;

		return nullptr;
	}

	ShapeRef Group::getShapeNamed( const std::string &name ) const {
		auto pos( _shapesByName.find( name ));
		if ( pos != _shapesByName.end() ) return pos->second;

		return nullptr;
	}

	GroupRef Group::find( const std::string &pathToChild, char separator ) const {
		if ( pathToChild.at(0) == separator ) {
			return getRoot()->find( pathToChild.substr(1), separator );
		}

		GroupRef node = const_cast<Group*>(this)->shared_from_this();
		strings::stringvec path = strings::split( pathToChild, separator );

		for( auto childName(path.begin()),end(path.end()); childName != end; ++childName ) {
			node = node->getGroupNamed( *childName );

			//
			//	if the node is empty, return it as a failure. if we're on the last element, return the node
			//

			if ( !node || ((childName+1) == end) ) return node;
		}

		return nullptr;
	}

	dvec2 Group::getDocumentSize() const {
		return getRoot()->_documentSize;
	}

	void Group::setPosition( const dvec2 &position ) {
		_localTransformPosition = position;
		_transformDirty = true;
	}

	void Group::setAngle( double a ) {
		_localTransformAngle = a;
		_transformDirty = true;
	}

	void Group::setScale( const dvec2 &scale ) {
		_localTransformScale = scale;
		_transformDirty = true;
	}

	void Group::setBlendMode( const BlendMode &bm ) {
		_blendMode = bm;
	}

	dvec2 Group::localToGlobal( const dvec2 &p ) {
		const dmat4 worldTransform = _worldTransform( dmat4(1) );
		return worldTransform * p;
	}

	dvec2 Group::globalToLocal( const dvec2 &p ) {
		const dmat4 worldTransform = _worldTransform( dmat4(1) );
		const dmat4 inverseWorldTransform = inverse(worldTransform);
		return inverseWorldTransform * p;
	}

	void Group::_draw( const render_state &state, double parentOpacity, const ci::gl::GlslProgRef &shader ) {
		auto self = shared_from_this();
		double opacity = _opacity * parentOpacity;
		BlendMode lastBlendMode = getBlendMode();

		glEnable( GL_BLEND );
		lastBlendMode.bind();

		_updateTransform();

		gl::ScopedModelMatrix smm;
		gl::multModelMatrix(_transform);

		for( auto c(_drawables.begin()),end(_drawables.end()); c!=end; ++c ) {
			if ( c->first ) {
				//
				//	Draw child group
				//

				c->first->_draw( state, opacity, shader );
			}
			else if ( c->second ) {
				//
				//	Draw child shape with its blend mode if one was set, or ours otherwise
				//

				if ( c->second->getBlendMode() != lastBlendMode ) {
					lastBlendMode = c->second->getBlendMode();
					lastBlendMode.bind();
				}

				c->second->draw( state, self, opacity, shader );
			}
		}
	}

	void Group::_parseGroupAttributes( const ci::XmlTree &groupNode ) {
		if ( groupNode.hasAttribute( "id" )) {
			_id = groupNode.getAttribute( "id" ).getValue();
		}

		if ( groupNode.hasAttribute( "transform" )) {
			_groupTransform = util::svg::parseTransform( groupNode.getAttribute( "transform" ).getValue() );
		}

		{
			util::svg::svg_style style = util::svg::parseStyle( groupNode );
			_opacity = style.opacity;
		}

		//
		//	Now cache all attributes, in case we want to query them later
		//

		for( const auto &attr : groupNode.getAttributes() ) {
			_attributes[ attr.getName() ] = attr.getValue();
		}
	}

	void Group::_loadGroupsAndShapes( const ci::XmlTree &fromNode ) {
		auto self = shared_from_this();
		for ( auto childNode = fromNode.begin(); childNode != fromNode.end(); ++childNode ) {
			const std::string tag = childNode->getTag();

			if ( tag == "g" ) {
				GroupRef child = make_shared<Group>();
				child->parse( *childNode );
				child->_parent = self;

				_groups.push_back( child );
				_groupsByName[ child->getId() ] = child;

				_drawables.push_back( drawable( child, nullptr) );
			}
			else if ( util::svg::canParseShape( tag )) {
				ShapeRef shape = make_shared<Shape>();

				shape->parse( *childNode );
				_shapes.push_back( shape );
				_shapesByName[ shape->getId() ] = shape;

				if ( shape->isOrigin() )
				{
					_originShape = shape;
				}

				_drawables.push_back( drawable( nullptr, shape ));
			}
		}
	}

	void Group::_updateTransform() {
		if ( _transformDirty ) {
			_transformDirty = false;

			const dmat4 S = glm::scale(dvec3(_localTransformScale, 1));
			const dmat4 T = glm::translate(dvec3(_localTransformPosition, 0));

			const dvec2 rotation(std::cos(_localTransformAngle),std::sin(_localTransformAngle));

			dmat4 R(1);
			R[0] = vec4(rotation.x, rotation.y, 0, 0);
			R[1] = vec4(-rotation.y, rotation.x, 0, 0);

			_transform = _groupTransform * ( T * S * R );
		}
	}

	void Group::_normalize( const dvec2 &documentSize, double documentScale, const dmat4 &worldTransform, const dvec2 &parentWorldOrigin ) {
		//
		//	Project shapes to world, and gather their collective bounds in world space
		//

		dmat4 toWorld = worldTransform * _groupTransform;

		Rectd worldBounds(
						  +std::numeric_limits<double>::max(),
						  +std::numeric_limits<double>::max(),
						  -std::numeric_limits<double>::max(),
						  -std::numeric_limits<double>::max()
						  );

		if ( !_shapes.empty() ) {
			for ( const auto &shape : _shapes ) {
				worldBounds.include(shape->_projectToWorld( documentSize, documentScale, toWorld ));
			}
		}
		else {
			worldBounds.set(0, 0, documentSize.x, documentSize.y );
		}

		//
		//	determine the origin of this object in world coordinates.
		//	if an 'origin' object was specified, use the center of its world bounds.
		//	otherwise use the center of the bounds of all our shapes.
		//

		dvec2 worldOrigin = worldBounds.getCenter();

		if ( _originShape ) {
			worldOrigin = _originShape->_worldBounds.getCenter();
		}

		//
		//	Now make the shapes geometry local to our worldOrigin
		//

		for (auto shape : _shapes) {
			shape->_makeLocal(worldOrigin);
		}

		//
		//	now set our transform to make our world origin local to our parent's world origin
		//

		_groupTransform = glm::translate(dvec3(worldOrigin - parentWorldOrigin, 0));

		//
		//	finally pass on to children
		//

		for (auto child : _groups) {
			child->_normalize(documentSize, documentScale, toWorld, worldOrigin);
		}

		//
		//	Now, finally, the root object should move its group transform to local, and make
		//	the group transform identity. This is because nobody 'owns' the root object, so
		//	its transform is inherently local.
		//

		if ( isRoot() ) {
			_localTransformPosition = worldOrigin - parentWorldOrigin;
			_groupTransform = dmat4(1);
		}
	}

	dmat4 Group::_worldTransform(dmat4 m) {
		if ( GroupRef p = getParent() ) {
			return p->_worldTransform(m);
		}
		
		_updateTransform();
		return m * _transform;
	}

}}} // core::util::svg