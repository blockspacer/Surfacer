//
//  TerrainDetail.hpp
//  Surfacer
//
//  Created by Shamyl Zakariya on 3/30/17.
//
//

#ifndef TerrainDetail_h
#define TerrainDetail_h

#include "TerrainWorld.hpp"

#include <cinder/Log.h>
#include <cinder/Triangulate.h>
#include <cinder/gl/gl.h>
#include <cinder/Rand.h>

#include <list>
#include <queue>
#include <functional>

#include <boost/geometry.hpp>
#include <boost/geometry/geometries/point_xy.hpp>
#include <boost/geometry/geometries/polygon.hpp>
#include <boost/geometry/multi/multi.hpp>

#include "ContourSimplification.hpp"

using namespace core;

namespace terrain {
	namespace detail {

		// TODO: Optimization is disabled since it seems to break edge stitching. The question is, how?
		const float RDP_CONTOUR_OPTIMIZATION_THRESHOLD = 0.5;

		const float MIN_SHAPE_AREA = 1.f;
		const float MIN_TRIANGLE_AREA = 1.f;
		const float COLLISION_SHAPE_RADIUS = 0.05;

#pragma mark - Helpers

		Rand colorRand(456);

		Color next_random_color() {
			return Color(CM_HSV, colorRand.nextFloat(), 0.5 + colorRand.nextFloat(0.5), 0.5 + colorRand.nextFloat(0.5));
		}

		vec2 rotate_cw(const vec2 &v) {
			return vec2(v.y, -v.x);
		}

		bool is_wound_clockwise(const PolyLine2f &contour) {
			// http://stackoverflow.com/questions/1165647/how-to-determine-if-a-list-of-polygon-points-are-in-clockwise-order
			// Sum over the edges, (x2 − x1)(y2 + y1)

			double sum = 0;
			for (size_t i = 0, j = 1, n = contour.getPoints().size(); i < n; i++, j++) {
				vec2 a = contour.getPoints()[i];
				vec2 b = contour.getPoints()[j % n];
				double k = (b.x - a.x) * (b.y + a.y);
				sum += k;
			}

			return sum >= 0;
		}

		void wind_clockwise(PolyLine2f &contour) {
			if (!is_wound_clockwise(contour)) {
				contour.reverse();
			}
		}

		void wind_counter_clockwise(PolyLine2f &contour) {
			if (is_wound_clockwise(contour)) {
				contour.reverse();
			}
		}

		PolyLine2f transformed(const PolyLine2f &pl, const mat4 &m) {
			PolyLine2f transformed;
			for (auto &p : pl) {
				transformed.push_back(m * p);
			}
			return transformed;
		}

		void transform(PolyLine2f &pl, const mat4 &m) {
			for (auto &p : pl) {
				vec2 tp = m * p;
				p.x = tp.x;
				p.y = tp.y;
			}
		}

		void transform(const PolyLine2f &a, PolyLine2f &b, const mat4 &m) {
			if (b.getPoints().size() != a.getPoints().size()) {
				b.getPoints().clear();
				b.getPoints().resize(a.getPoints().size());
			}

			auto ai = begin(a);
			auto bi = begin(b);
			const auto end = ::end(a);
			for (; ai != end; ++ai, ++bi) {
				*bi = m * (*ai);
			}
		};

		PolyLine2f optimize(PolyLine2f p) {
			if (RDP_CONTOUR_OPTIMIZATION_THRESHOLD > 0) {
				return simplify(p, RDP_CONTOUR_OPTIMIZATION_THRESHOLD);
			} else {
				p.setClosed();
				return p;
			}
		}

		vector<Shape::contour_pair> optimize(vector<PolyLine2f> ps) {
			vector<Shape::contour_pair> ret;

			if (RDP_CONTOUR_OPTIMIZATION_THRESHOLD > 0) {
				for (auto &p : ps) {
					ret.push_back(simplify(p, RDP_CONTOUR_OPTIMIZATION_THRESHOLD));
				}
			} else {
				for (auto &p : ps) {
					p.setClosed();
					ret.push_back(p);
				}
			}

			return ret;
		}


#pragma mark - Boost::Geometry - Shape Interop


		typedef boost::geometry::model::polygon<boost::geometry::model::d2::point_xy<double> > polygon;

		std::vector<ShapeRef> convertBoostGeometryToTerrainShapes( std::vector<polygon> &polygons, const mat4 &modelview) {
			const float Dist2Epsilon = 1e-2;
			std::vector<ShapeRef> result;

			for( std::vector<polygon>::iterator outIt = polygons.begin(); outIt != polygons.end(); ++outIt ) {
				typedef polygon::inner_container_type::const_iterator RingIterator;
				typedef polygon::ring_type::const_iterator PointIterator;

				boost::geometry::correct(*outIt);

				bool polygonOuterContourIsSane = true;
				PolyLine2f shapeContour;
				vector<PolyLine2f> holeContours;

				// convert the boost geometry to PolyLine2f, creating the shapeContour and holeContours.
				// note: we apply the modelview BEFORE calling updateModelview on the PolyShape. This
				// is to move the polylines to the world space of the shape they were created from,
				// and then move them to their own model space. this is a small loss of precision
				// (transforming and storing vertices!) but it solves problem of cutting a rotated poly

				for( PointIterator pt = outIt->outer().begin(); pt != outIt->outer().end(); ++pt ) {
					const double x = boost::geometry::get<0>(*pt);
					const double y = boost::geometry::get<1>(*pt);
					if ( isinf(x) || isinf(y)) {
						CI_LOG_E("failed isinf() test on outer shape of polygon: " << (size_t)(outIt - polygons.begin()) << " vertex: " << (size_t)(pt - outIt->outer().begin()));
						polygonOuterContourIsSane = false;
						break;
					} else {
						shapeContour.push_back( vec2(x,y) );
					}
				}

				if (!polygonOuterContourIsSane) {
					CI_LOG_E("Skipping this polygon entirely as outer contour failed");
					break;
				}

				// excise double first-last vertices
				if (distanceSquared(shapeContour.getPoints().front(), shapeContour.getPoints().back()) < Dist2Epsilon) {
					shapeContour.getPoints().pop_back();
				}

				if (shapeContour.getPoints().empty()) {
					CI_LOG_E("Polygon is empty");
					break;
				}

				transform(shapeContour, modelview);

				for( RingIterator crunk = outIt->inners().begin(); crunk != outIt->inners().end(); ++crunk ) {
					PolyLine2f contour;
					bool innerContourIsSane = true;
					for( PointIterator pt = crunk->begin(); pt != crunk->end(); ++pt ) {
						const double x = boost::geometry::get<0>(*pt);
						const double y = boost::geometry::get<1>(*pt);
						if ( isinf(x) || isinf(y)) {
							innerContourIsSane = false;
							CI_LOG_E("failed isinf() test on inner shape of polygon: " << (size_t)(outIt - polygons.begin())
									 << " ring: " << (size_t)(crunk - outIt->inners().begin())
									 << " vertex: " << (size_t)(pt - crunk->begin()));
							break;
						} else {
							contour.push_back( vec2(x,y) );
						}
					}

					if (!innerContourIsSane) {
						CI_LOG_E("Skipping this inner contour");
						break;
					}

					// excise double first-last vertices
					if (distanceSquared(contour.getPoints().front(), contour.getPoints().back()) < Dist2Epsilon) {
						contour.getPoints().pop_back();
					}

					if (!contour.getPoints().empty() && contour.calcArea() >= MIN_SHAPE_AREA ) {
						transform(contour, modelview);
						holeContours.push_back( contour );
					}
				}

				// if we're here we're good
				result.push_back(make_shared<Shape>(shapeContour, holeContours));
			}

			return result;
		}

		polygon convertTerrainShapeToBoostGeometry( ShapeRef shape ) {
			polygon result;

			for (auto &p : shape->getOuterContour().model.getPoints()) {
				result.outer().push_back( boost::geometry::make<boost::geometry::model::d2::point_xy<double> >( p.x, p.y ) );
			}

			for (auto &holeContour : shape->getHoleContours()) {
				polygon::ring_type ring;
				for( auto &p : holeContour.model.getPoints() ) {
					ring.push_back( boost::geometry::make<boost::geometry::model::d2::point_xy<double> >( p.x, p.y ) );
				}
				result.inners().push_back( ring );
			}

			boost::geometry::correct( result );
			return result;
		}

		polygon convertPolyLineToBoostGeometry( const PolyLine2f &polyLine )
		{
			polygon result;

			for (auto &p : polyLine.getPoints()) {
				result.outer().push_back( boost::geometry::make<boost::geometry::model::d2::point_xy<double> >( p.x, p.y ) );
			}

			boost::geometry::correct( result );
			return result;
		}


#pragma mark - Contour Soup


		class contour_tree_node;
		typedef shared_ptr<contour_tree_node> contour_tree_node_ref;

		struct contour_tree_node {
			PolyLine2f contour;
			vector<contour_tree_node_ref> children;
		};

		/**
		 Given a "soup" of contours (an unordered mess of closed polylines) build a tree (with possibly
		 multiple roots) which describes their nesting in a manner useful for constructing PolyShapes
		 */
		vector<contour_tree_node_ref> build_contour_tree(const vector<PolyLine2f> &contourSoup) {

			// simple cases
			if (contourSoup.size() == 0) {
				return vector<contour_tree_node_ref>();
			} else if (contourSoup.size() == 1) {
				contour_tree_node_ref node = make_shared<contour_tree_node>();
				node->contour = contourSoup.front();
				vector<contour_tree_node_ref> ret = { node };
				return ret;
			}


			// create a set of pointers to our polylines
			set<const PolyLine2f*> contours;
			for (auto &pl : contourSoup) {
				contours.insert(&pl);
			}

			vector<contour_tree_node_ref> rootNodes;
			while(!contours.empty()) {


				contour_tree_node_ref node = make_shared<contour_tree_node>();
				rootNodes.push_back(node);

				// find first polyline which is not a hole in another polyline. remove it
				for (auto candidateIt = begin(contours); candidateIt != end(contours); ++candidateIt) {
					bool isHole = false;
					for (auto it2 = begin(contours); it2 != end(contours); ++it2) {
						if (candidateIt != it2) {
							if ((*it2)->contains(*(*candidateIt)->begin())) {
								isHole = true;
								break;
							}
						}
					}

					// we found a non-hole contour; remove candidate from set and break loop
					if (!isHole) {
						node->contour = **candidateIt;
						contours.erase(candidateIt);
						break;
					}
				}

				// find each polyline which is inside the one we just found
				vector<PolyLine2f> innerContours;
				for (auto candidateHoleIt = begin(contours); candidateHoleIt != end(contours);) {
					if (node->contour.contains(*(*candidateHoleIt)->begin())) {
						innerContours.push_back(**candidateHoleIt);
						candidateHoleIt = contours.erase(candidateHoleIt);
					} else {
						++candidateHoleIt;
					}
				}

				// now we know innerContours are children of node->outerContour - but
				// only those with no children may become holes. Any which have children
				// themselves must become new root nodes

				auto children = build_contour_tree(innerContours);
				node->children.insert(end(node->children), begin(children), end(children));
			}

			return rootNodes;
		}

		vector<ShapeRef> build_from_contour_tree(contour_tree_node_ref rootNode, int depth) {
			if (depth % 2) {
				CI_LOG_E("build_from_contour_tree MUST only be called on even depths");
				assert(false);
			}

			// simplest case, no child nodes
			if (rootNode->children.empty()) {
				vector<ShapeRef> r = { make_shared<Shape>(rootNode->contour) };
				return r;
			}

			// we can make a PolyShape from this node's contour and each child which has no children
			// each child which does have children we have to recurse

			vector<ShapeRef> result;

			vector<PolyLine2f> holes;
			for (auto &childNode : rootNode->children) {
				// each child becomes a hole contour
				holes.push_back(childNode->contour);

				// for each grandchild, we're creating a new PolyShape
				for (auto &grandChild : childNode->children) {
					auto childResult = build_from_contour_tree(grandChild, depth+2);
					result.insert(end(result), begin(childResult), end(childResult));
				}
			}

			result.push_back(make_shared<Shape>(rootNode->contour, holes));

			return result;
		}

#pragma mark - Sorted Ref Pair

		template<typename T>
		class sorted_ref_pair {
		public:
			sorted_ref_pair(const shared_ptr<T> &A, const shared_ptr<T> &B) {
				if (A < B) {
					a = A;
					b = B;
				} else {
					a = B;
					b = A;
				}
			}

			friend bool operator == (const sorted_ref_pair<T> &a, const sorted_ref_pair<T> &b) {
				return a.a == b.a && a.b == b.b;
			}

			friend bool operator < (const sorted_ref_pair<T> &a, const sorted_ref_pair<T> &b) {
				if (a.a != b.a) {
					return a.a < b.a;
				}
				return a.b < b.b;
			}

		private:
			shared_ptr<T> a,b;
		};

#pragma mark - Edge Test

		bool shared_edges(const ShapeRef &a, const ShapeRef &b) {
			const float bbFudge = 4.f;
			if (cpBBIntersects(a->getWorldSpaceContourEdgesBB(), b->getWorldSpaceContourEdgesBB(), bbFudge)) {

				const auto &aShapeEdges = a->getWorldSpaceContourEdges();
				const auto &bShapeEdges = b->getWorldSpaceContourEdges();
				for (const auto &pe : bShapeEdges) {
					if (aShapeEdges.find(pe) != aShapeEdges.end()) {
						return true;
					}
				}
			}
			return false;
		}

#pragma mark - Flood Fill

		template<typename T>
		bool contains(const set<T> &s, const T &v) {
			return s.find(v) != s.end();
		}

		GroupBaseRef getParentGroup(const ShapeRef &shape, const map<ShapeRef,GroupBaseRef> &parentage) {
			if (shape->getGroup()) {
				return shape->getGroup();
			} else {
				auto pos = parentage.find(shape);
				if (pos != parentage.end()) {
					return pos->second;
				}
			}

			return nullptr;
		};

		set<ShapeRef> findGroup(ShapeRef origin, const set<ShapeRef> &all, const map<ShapeRef,GroupBaseRef> &parentage) {
			/*
			 Very loosely adapted from Wikipedia's FloodFill page:
			 http://en.wikipedia.org/wiki/Flood_fill
			 */

			queue<ShapeRef> Q;
			Q.push(origin);
			set<ShapeRef> group;

			while(!Q.empty()) {
				ShapeRef currentShape = Q.front();
				Q.pop();

				if (!contains(group, currentShape)) {
					group.insert(currentShape);
					const auto currentShapeParent = getParentGroup(currentShape, parentage);

					// now find all connected neighbors with shared parentage, add them to group and Q

					for (auto queryShape : all) {
						if (queryShape != currentShape && !contains(group,queryShape)) {
							const auto queryShapeParent = getParentGroup(queryShape, parentage);
							if (queryShapeParent == currentShapeParent && shared_edges(queryShape, currentShape)) {
								Q.push(queryShape);
							}
						}
					}
				}
			}
			
			return group;
		}
		
		
	}
}

#endif /* TerrainDetail_h */