---
plan name: havel-math-pure
plan description: Pure Havel math/physics modules
plan status: done
---

## Idea
Remove C++ PhysicsModule entirely and slim down C++ MathModule to only thin `<cmath>` host bridges. Build a rich pure-Havel math/physics module hierarchy under modules/lang/math/:

math.hv - base constants (PI, E, TAU, SQRT2), utility functions (clamp, lerp, sign, fract, deg2rad, rad2deg, mod, distance, hypot, log2, log10, cbrt, asin, acos, atan), and the "math" namespace object that wraps C++ host bridges + pure-Havel additions

physics.hv - all physics constants (C, G, G0, K, NA, R, ME, MP, E_CHARGE, H, EPS0, MU0, plus extras like c, e, me, h, mp, k, epsilon0, mu0, alpha, sigma, a0, re, Rinf, u, eV) and physics functions (force, kinetic_energy, potential_energy, momentum, wavelength, de_broglie, kepler_period, escape_velocity, ohm_law, gravitational_force). Sets up both "physics" and "Physics" globals, plus bare constant globals for backward compat.

vec.hv - Vec2, Vec3, Mat4, Quat structs with GLM-style operations (dot, cross, norm, lookAt, perspective, rotate, translate, scale)

num.hv - NDArray struct for N-dimensional arrays with map, sum, mean, broadcasting

stats.hv - mean, median, std, variance, correlation, distributions

sci.hv - numerical integration, ODE solvers, FFT, optimization

symbolic.hv - expression trees, simplification, differentiation, substitution

set.hv - set operations (union, intersect, difference, subset, symmetric difference)

special.hv - gamma, beta, erf, bessel, combinatorics

Key design: C++ MathModule stays but ONLY registers host bridges for cmath functions that need native code (sin, cos, tan, sqrt, log, exp, pow, ceil, floor, round, abs). It still builds the "math" namespace object and loads modules/lang/math/math.hv sidecar which adds all pure-Havel functions as fields. PhysicsModule.cpp is completely removed. The physics.hv module is loaded by MathModule after math.hv (or directly by VM module loading).

Backward compatibility: bare globals PI, E, G, C remain. Physics functions (force, etc.) remain as bare globals via physics.hv exports. The "physics" object gets C, G, etc. as fields.

## Implementation
- Slim down C++ MathModule.cpp to only cmath host bridges (sin, cos, tan, asin, acos, atan, sqrt, log, exp, pow, ceil, floor, round, abs) + build math namespace object + load modules/lang/math/math.hv sidecar
- Remove C++ PhysicsModule.cpp and PhysicsModule.hpp entirely
- Remove registerPhysicsModule from PureStdLib.cpp and StdLibModules.cpp
- Update CMakeLists to remove PhysicsModule.cpp from build
- Create modules/lang/math/ directory structure
- Write modules/lang/math/math.hv with constants (PI, E, TAU, SQRT2) and pure-Havel utility functions (clamp, lerp, sign, fract, deg2rad, rad2deg, mod, distance, hypot, log2, log10, cbrt, min, max, random, randint)
- Write modules/lang/math/physics.hv with all physics constants (as both module fields and bare globals) and physics functions (force, kinetic_energy, etc.)
- Update C++ MathModule to also load modules/lang/math/physics.hv after math.hv, creating physics/Physics global objects
- Write modules/lang/math/vec.hv with Vec2, Vec3, Mat4, Quat structs
- Write modules/lang/math/num.hv with NDArray struct
- Write modules/lang/math/stats.hv with statistical functions
- Write modules/lang/math/sci.hv with numerical computation functions
- Write modules/lang/math/symbolic.hv with expression tree support
- Write modules/lang/math/set.hv with set theory operations
- Write modules/lang/math/special.hv with special math functions
- Update smoke tests and integration tests to work with new structure
- Build and run full test suite to verify no regressions

## Required Specs
<!-- SPECS_START -->
- remove-call-host
- bootstrap-compiler
<!-- SPECS_END -->