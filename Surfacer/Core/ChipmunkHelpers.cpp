//
//  ChipmunkHelpers.cpp
//  Surfacer
//
//  Created by Shamyl Zakariya on 4/30/12.
//  Copyright (c) 2012 __MyCompanyName__. All rights reserved.
//

#include "Common.h"
#include <algorithm>


bool cpBBIntersection( const cpBB &bb1, const cpBB &bb2, cpBB &intersection )
{
	if ( !cpBBIntersects( bb1, bb2 )) return false;

	cpFloat range[4];
	range[0] = bb1.l;
	range[1] = bb1.r;
	range[2] = bb2.l;
	range[3] = bb2.r;
	std::sort( range, range + 4 );
	
	intersection.l = range[1];
	intersection.r = range[2];

	range[0] = bb1.b;
	range[1] = bb1.t;
	range[2] = bb2.b;
	range[3] = bb2.t;
	std::sort( range, range + 4 );

	intersection.b = range[1];
	intersection.t = range[2];
	
	return true;
}

