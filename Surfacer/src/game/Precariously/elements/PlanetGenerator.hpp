//
//  PlanetGenerator.hpp
//  Surfacer
//
//  Created by Shamyl Zakariya on 2/1/18.
//

#ifndef PlanetGenerator_hpp
#define PlanetGenerator_hpp


#include "Core.hpp"
#include "TerrainWorld.hpp"

namespace precariously { namespace planet_generation {
    
    /**
     Parameters to tune planet generation
    */
    struct params {
        
        // size of map
        int size;

        // world is generated by default 1 to 1 mapping to pixels of the generated maps. Scale and or translate
        // to move the result geoemtry to a more useful position. See params::defaultCenteringTransform
        dmat4 transform;
        
        struct generation_params {
            // random seed to start with
            int seed;
            
            // number of perlin noise octaves
            int noiseOctaves;
            
            // scale frequency of perlin noise sampling
            double noiseFrequencyScale;
            
            // Value of zero results in a very wispy planet surface, and 1 results in a very solid surface
            double surfaceSolidity;
            
            double vignetteStart;
            
            double vignetteEnd;

            generation_params():
            seed(12345),
            noiseOctaves(4),
            noiseFrequencyScale(1),
            surfaceSolidity(1),
            vignetteStart(0.9),
            vignetteEnd(1)
            {}
        };

        struct terrain_params : public generation_params {
            // if true, terrain will be generated
            bool enabled;
            
            // if > 0 the terrain will be partitioned, which can improve draw/cut performance
            int partitionSize;
            
            // if true, terrain (not anchors) will be pruned of all but the biggest solid geometry
            bool pruneFloaters;
            
            // material to apply to terrain
            terrain::material material;
            
            terrain_params(bool enabled=true):
            enabled(enabled),
            partitionSize(0),
            pruneFloaters(true)
            {}

        } terrain;
        
        
        struct anchor_params : public generation_params {
            // if true, anchors will be generated
            bool enabled;
            
            // material to apply to anchors
            terrain::material material;
            
            anchor_params(bool enabled=true):
            enabled(enabled)
            {}
        } anchors;
        
        
        struct attachment_params {
            
            // if the dot value of surface normal against local up is between minUpDot and maxUpDot the
            // probability of a greeble planting will increase by similarity to up. Range [-1,+1]
            double minUpDot;
            double maxUpDot;
            
            // dice roll probability of a valid surface point getting a planting. Range [0,+1]
            double probability;
            
            // number of possible plantings per unit distance
            double density;
            
            // id to query later to get the generated attachments
            size_t id;

            attachment_params():
            minUpDot(-1),
            maxUpDot(+1),
            probability(1),
            density(1),
            id(0)
            {}
            
            attachment_params(size_t id, double minUpDot, double maxUpDot, double probability, double density):
            minUpDot(minUpDot),
            maxUpDot(maxUpDot),
            probability(probability),
            density(density),
            id(id)
            {}

        };
        
        // each attachment_params added will cause an attachment generating pass on the generated terrain;
        vector<attachment_params> attachments;
        
        params(int size=512):
        size(size)
        {}
        
        /**
         apply a default centering transform that centers the generated geometry at the origin, with an optional scaling factor
         */
        params &defaultCenteringTransform(double scale = 1) {
            this->transform = glm::scale(dvec3(scale, scale, 1)) * glm::translate(dvec3(-size/2, -size/2, 0));
            return *this;
        }
    };
    
    namespace detail {

        /**
         Generate a map given the provided parameters
         */
        Channel8u generate_map(const params::generation_params &p, int size);

        /**
         Generate just terrain map from given generation parameters,
         and populate terrain::Shape vector with the marched/triangulated geometry.
         return terrain map
         */
        ci::Channel8u generate_shapes(const params &p, vector <terrain::ShapeRef> &shapes);

        /**
         Generate just anchors and anchor map from given generation parameters,
         and populate terrain::Shape vector with the marched/triangulated geometry.
         return terrain map
         */
        ci::Channel8u generate_anchors(const params &p, vector <terrain::AnchorRef> &anchors);

    }
    
    struct result {
        terrain::WorldRef world;
        ci::Channel8u terrainMap;
        ci::Channel8u anchorMap;
        map<size_t,vector<terrain::AttachmentRef>> attachmentsById;
    };

    /**
     Generate a terrin::World with given generation parameters; generates both terrain and anchor geometry.
     */
    result generate(const params &params, terrain::WorldRef world);

    /**
     Generate a terrin::World with given generation parameters; generates both terrain and anchor geometry.
     */
    result generate(const params &params, core::SpaceAccessRef space);
    
}} // end namespace precariously::planet_generation

#endif /* PlanetGenerator_hpp */
