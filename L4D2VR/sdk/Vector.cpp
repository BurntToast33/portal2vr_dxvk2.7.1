#include "vector.h"

void ConcatTransforms(const matrix3x4_t& in1, const matrix3x4_t& in2, matrix3x4_t& out)
{
#if 0
	// test for ones that'll be 2x faster
	if ((((size_t)&in1) % 16) == 0 && (((size_t)&in2) % 16) == 0 && (((size_t)&out) % 16) == 0)
	{
		ConcatTransforms_Aligned(in1, in2, out);
		return;
	}
#endif

	fltx4 lastMask = *(fltx4*)(&g_SIMD_ComponentMask[3]);
	fltx4 rowA0 = LoadUnalignedSIMD(in1.m_flMatVal[0]);
	fltx4 rowA1 = LoadUnalignedSIMD(in1.m_flMatVal[1]);
	fltx4 rowA2 = LoadUnalignedSIMD(in1.m_flMatVal[2]);

	fltx4 rowB0 = LoadUnalignedSIMD(in2.m_flMatVal[0]);
	fltx4 rowB1 = LoadUnalignedSIMD(in2.m_flMatVal[1]);
	fltx4 rowB2 = LoadUnalignedSIMD(in2.m_flMatVal[2]);

	// now we have the rows of m0 and the columns of m1
	// first output row
	fltx4 A0 = SplatXSIMD(rowA0);
	fltx4 A1 = SplatYSIMD(rowA0);
	fltx4 A2 = SplatZSIMD(rowA0);
	fltx4 mul00 = MulSIMD(A0, rowB0);
	fltx4 mul01 = MulSIMD(A1, rowB1);
	fltx4 mul02 = MulSIMD(A2, rowB2);
	fltx4 out0 = AddSIMD(mul00, AddSIMD(mul01, mul02));

	// second output row
	A0 = SplatXSIMD(rowA1);
	A1 = SplatYSIMD(rowA1);
	A2 = SplatZSIMD(rowA1);
	fltx4 mul10 = MulSIMD(A0, rowB0);
	fltx4 mul11 = MulSIMD(A1, rowB1);
	fltx4 mul12 = MulSIMD(A2, rowB2);
	fltx4 out1 = AddSIMD(mul10, AddSIMD(mul11, mul12));

	// third output row
	A0 = SplatXSIMD(rowA2);
	A1 = SplatYSIMD(rowA2);
	A2 = SplatZSIMD(rowA2);
	fltx4 mul20 = MulSIMD(A0, rowB0);
	fltx4 mul21 = MulSIMD(A1, rowB1);
	fltx4 mul22 = MulSIMD(A2, rowB2);
	fltx4 out2 = AddSIMD(mul20, AddSIMD(mul21, mul22));

	// add in translation vector
	A0 = AndSIMD(rowA0, lastMask);
	A1 = AndSIMD(rowA1, lastMask);
	A2 = AndSIMD(rowA2, lastMask);
	out0 = AddSIMD(out0, A0);
	out1 = AddSIMD(out1, A1);
	out2 = AddSIMD(out2, A2);

	// write to output
	StoreUnalignedSIMD(out.m_flMatVal[0], out0);
	StoreUnalignedSIMD(out.m_flMatVal[1], out1);
	StoreUnalignedSIMD(out.m_flMatVal[2], out2);
}

QAngle TransformAnglesToWorldSpace(const QAngle& angles, const matrix3x4_t& parentMatrix)
{
	matrix3x4_t angToParent, angToWorld;
	AngleMatrix(angles, angToParent);
	ConcatTransforms(parentMatrix, angToParent, angToWorld);
	QAngle out;
	MatrixAngles(angToWorld, out);
	return out;
}

void MatrixAngles(const matrix3x4_t& matrix, float* angles)
{
	float forward[3];
	float left[3];
	float up[3];

	//
	// Extract the basis vectors from the matrix. Since we only need the Z
	// component of the up vector, we don't get X and Y.
	//
	forward[0] = matrix[0][0];
	forward[1] = matrix[1][0];
	forward[2] = matrix[2][0];
	left[0] = matrix[0][1];
	left[1] = matrix[1][1];
	left[2] = matrix[2][1];
	up[2] = matrix[2][2];

	float xyDist = sqrtf(forward[0] * forward[0] + forward[1] * forward[1]);

	// enough here to get angles?
	if (xyDist > 0.001f)
	{
		// (yaw)	y = ATAN( forward.y, forward.x );		-- in our space, forward is the X axis
		angles[1] = RAD2DEG(atan2f(forward[1], forward[0]));

		// (pitch)	x = ATAN( -forward.z, sqrt(forward.x*forward.x+forward.y*forward.y) );
		angles[0] = RAD2DEG(atan2f(-forward[2], xyDist));

		// (roll)	z = ATAN( left.z, up.z );
		angles[2] = RAD2DEG(atan2f(left[2], up[2]));
	}
	else	// forward is mostly Z, gimbal lock-
	{
		// (yaw)	y = ATAN( -left.x, left.y );			-- forward is mostly z, so use right for yaw
		angles[1] = RAD2DEG(atan2f(-left[0], left[1]));

		// (pitch)	x = ATAN( -forward.z, sqrt(forward.x*forward.x+forward.y*forward.y) );
		angles[0] = RAD2DEG(atan2f(-forward[2], xyDist));

		// Assume no roll in this case as one degree of freedom has been lost (i.e. yaw == roll)
		angles[2] = 0;
	}
}

void MatrixCopy(const matrix3x4_t& in, matrix3x4_t& out)
{
	memcpy(out.Base(), in.Base(), sizeof(float) * 3 * 4);
}

void AngleMatrix(const QAngle& angles, matrix3x4_t& matrix)
{
	float sr, sp, sy, cr, cp, cy;

	SinCos(angles[YAW] * PRECALC_DEG_TO_RAD, &sy, &cy);
	SinCos(angles[PITCH] * PRECALC_DEG_TO_RAD, &sp, &cp);
	SinCos(angles[ROLL] * PRECALC_DEG_TO_RAD, &sr, &cr);

	// matrix = (YAW * PITCH) * ROLL
	matrix[0][0] = cp * cy;
	matrix[1][0] = cp * sy;
	matrix[2][0] = -sp;

	// NOTE: Do not optimize this to reduce multiplies! optimizer bug will screw this up.
	matrix[0][1] = sr * sp * cy + cr * -sy;
	matrix[1][1] = sr * sp * sy + cr * cy;
	matrix[2][1] = sr * cp;
	matrix[0][2] = (cr * sp * cy + -sr * -sy);
	matrix[1][2] = (cr * sp * sy + -sr * cy);
	matrix[2][2] = cr * cp;

	matrix[0][3] = 0.0f;
	matrix[1][3] = 0.0f;
	matrix[2][3] = 0.0f;
}