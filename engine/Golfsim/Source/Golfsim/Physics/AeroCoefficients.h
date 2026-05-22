// Tunable aerodynamic coefficients + environment for the ball-flight solver.
//
// Defaults reproduce published golf-ball drag/lift-vs-spin-ratio data (Bearman & Harvey 1976;
// Smits & Smith 1994) and are tuned to land the Trackman PGA-Tour averages inside the tolerances
// asserted in Tests/BallFlightTests.cpp. Treat every field here as a knob: the solver reads them,
// it never hard-codes them.

#pragma once

#include "CoreMinimal.h"

struct FAeroCoefficients
{
	// Lift coefficient vs spin ratio S = |omega| * r / V:
	//   Cl(S) = Clamp(Cl0 + Cl1 * S + Cl2 * S^2, 0, ClMax)
	// Empirical convex fit over the golf operating range (S ~ 0.05..0.55), tuned so the Trackman
	// PGA-average shots land near their published carry/apex across the whole bag (driver -> wedge).
	double Cl0 = 0.11;
	double Cl1 = 0.68;
	double Cl2 = -0.17;
	double ClMax = 0.45;

	// Drag coefficient vs spin ratio (and optionally Reynolds number Re):
	//   Cd(S, Re) = Cd0 + Cd1 * S + Cd2 * S^2 + kRe * (Re - ReRef)
	double Cd0 = 0.21;
	double Cd1 = 0.34;
	double Cd2 = 0.0;
	double CdMax = 0.50;     // upper clamp (effectively off at these spins); guards pathological input.
	double kRe = 0.0;        // Reynolds drag-crisis term; off by default.
	double ReRef = 1.1e5;

	// Spin decay: |omega(t)| = |omega0| * exp(-t / SpinDecayTau)
	double SpinDecayTau = 20.0;   // seconds

	// Environment
	double AirDensity = 1.225;    // kg/m^3 (sea-level ISA; lower for altitude/heat)
};
