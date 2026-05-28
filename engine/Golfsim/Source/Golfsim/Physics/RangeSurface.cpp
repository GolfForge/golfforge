#include "Physics/RangeSurface.h"

namespace
{
	// Mirror of build_range_splatmap.py constants (all yards -> meters). Keep these identical.
	constexpr double YD = 0.9144;             // m per yard (exact)
	constexpr double LANE_LEN_YD = 400.0;     // X extent of the open lane (tee at -X end)
	constexpr double LANE_WID_YD = 70.0;      // Y width of the open lane
	constexpr double TREE_WALL_YD = 22.0;     // depth of the tree wall framing the lane
	constexpr double FAIRWAY_WID_YD = 50.0;   // Y width of the centered mown strip
	constexpr double TEE_LEN_YD = 15.0;       // X length of the tee box (at the -X end)
	constexpr double TEE_WID_YD = 12.0;       // Y width of the tee box
}

namespace GolfRangeSurface
{
	EGolfLie ClassifyLie(double WorldXm, double WorldYm)
	{
		const double LaneHalfLen = (LANE_LEN_YD * YD) / 2.0;
		const double LaneHalfWid = (LANE_WID_YD * YD) / 2.0;
		const double Wall = TREE_WALL_YD * YD;

		const double Ax = FMath::Abs(WorldXm);
		const double Ay = FMath::Abs(WorldYm);

		if (Ax > LaneHalfLen || Ay > LaneHalfWid)
		{
			// Outside the open lane: the framing tree wall, else hidden rough. Trees aren't a roll
			// surface in EGolfLie, so a ball there is treated as Rough (grabs, near-dead).
			if (Ax <= LaneHalfLen + Wall && Ay <= LaneHalfWid + Wall)
			{
				return EGolfLie::Rough;   // tree wall
			}
			return EGolfLie::Rough;
		}

		// Inside the lane: tee box at the -X end, centered fairway strip, else rough shoulders.
		const double Tx0 = -LaneHalfLen;
		const double Tx1 = -LaneHalfLen + TEE_LEN_YD * YD;
		if (WorldXm >= Tx0 && WorldXm <= Tx1 && Ay <= (TEE_WID_YD * YD) / 2.0)
		{
			return EGolfLie::Tee;
		}
		if (Ay <= (FAIRWAY_WID_YD * YD) / 2.0)
		{
			return EGolfLie::Fairway;
		}
		return EGolfLie::Rough;
	}
}
