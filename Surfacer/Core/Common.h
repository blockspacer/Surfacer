#pragma once

/*
 *  Common.h
 *  CuttingExperiments
 *
 *  Created by Shamyl Zakariya on 12/22/10.
 *  Copyright 2010 Shamyl Zakariya. All rights reserved.
 *
 */

#include <map>
#include <set>
#include <vector>
#include <sstream>
#include <cmath>

#include <boost/foreach.hpp>

#include <cinder/Color.h>
#include <cinder/Filesystem.h>
#include <cinder/Matrix.h>
#include <cinder/Vector.h>
#include <cinder/Rand.h>
#include <cinder/Rect.h>

#include <chipmunk/chipmunk.h>

#define foreach BOOST_FOREACH
#define reverse_foreach BOOST_REVERSE_FOREACH

#define CLASS_NAME(ptr)		((typeid(*(ptr))).name())
#define SMART_PTR(cname)	class cname; typedef boost::shared_ptr< class cname > cname ## Ref; typedef boost::weak_ptr< class cname > cname ## WeakRef;


#if CP_USE_DOUBLES

	#define SINGLE_PRECISION 0
	#define DOUBLE_PRECISION 1
	
	#define GL_REAL GL_DOUBLE

	typedef double real;
	const real Epsilon = 4.37114e-05;

#else

	#define SINGLE_PRECISION 1
	#define DOUBLE_PRECISION 0
	
	#define GL_REAL GL_FLOAT

	typedef float real;
	const real Epsilon = 1e-4;

#endif

// 1 / 255
#define ALPHA_EPSILON real(0.00392156862745)

typedef double seconds_t;

#pragma mark - Helper APIs in global namespace

#include "MathHelpers.h"
#include "ChipmunkHelpers.h"
#include "StringHelpers.h"
#include "Exception.h"

