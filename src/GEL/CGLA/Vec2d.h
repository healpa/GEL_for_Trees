/* ----------------------------------------------------------------------- *
 * This file is part of GEL, http://www.imm.dtu.dk/GEL
 * Copyright (C) the authors and DTU Informatics
 * For license and list of authors, see ../../doc/intro.pdf
 * ----------------------------------------------------------------------- */

/** @file Vec2d.h
 * @brief 2D double vector class.
 */

#ifndef __CGLA_VEC2D_H__
#define __CGLA_VEC2D_H__

#include <GEL/CGLA/ArithVec2Float.h>
#include <GEL/CGLA/Vec2i.h>
#include <GEL/CGLA/Vec2f.h>


namespace CGLA {

	/** \brief 2D double floating point vector */

	class Vec2d: public ArithVec2Float<double,Vec2d>
	{
	public:

		Vec2d() {}

		Vec2d(double _a,double _b): 
			ArithVec2Float<double,Vec2d>(_a,_b) {}

		explicit Vec2d(const Vec2i& v): 
			ArithVec2Float<double,Vec2d>(v[0],v[1]) {}

		explicit Vec2d(const Vec2f& v): 
			ArithVec2Float<double,Vec2d>(v[0],v[1]) {}

		explicit Vec2d(double a): 
			ArithVec2Float<double,Vec2d>(a,a) {}
  
	};
    
    class Mat2x2d;
    template<> struct VecT_to_MatT<Vec2d> {using MatT = Mat2x2d;};

}
#endif
