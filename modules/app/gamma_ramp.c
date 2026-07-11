#include <math.h>
#include <stdint.h>

static double clamp01(double v, double lo, double hi) {
  return v < lo ? lo : (v > hi ? hi : v);
}

static void kelvin_to_rgb(double kelvin, double *r, double *g, double *b) {
  double k = clamp01(kelvin, 1000.0, 25000.0) / 100.0;
  if (k <= 66.0) {
    *r = 255.0;
    *g = clamp01(99.4708025861 * log(k) - 161.1195681661, 0.0, 255.0);
    *b = k <= 19.0 ? 0.0 : clamp01(138.5177312231 * log(k - 10.0) - 305.0447927307, 0.0, 255.0);
  } else {
    *r = clamp01(329.698727446 * pow(k - 60.0, -0.1332047592), 0.0, 255.0);
    *g = clamp01(288.1221695283 * pow(k - 60.0, -0.0755148492), 0.0, 255.0);
    *b = 255.0;
  }
  *r /= 255.0; *g /= 255.0; *b /= 255.0;
}

static double apply_shadow_lift(double ch, double lift) {
  double norm = clamp01(ch, 0.0, 1.0);
  double gamma_adj = 1.0 / (1.0 + lift);
  double gamma_lifted = pow(norm, gamma_adj);
  double additive_lifted = lift * (1.0 - norm);
  return clamp01(gamma_lifted + additive_lifted, 0.0, 1.0);
}

void fill_gamma_ramp(
  uint16_t *red, uint16_t *green, uint16_t *blue,
  int gamma_size,
  double brightness,
  double gamma_r, double gamma_g, double gamma_b,
  double temp_k,
  double shadow_lift)
{
  double tr, tg, tb;
  kelvin_to_rgb(temp_k, &tr, &tg, &tb);

  double inv_r = 1.0 / gamma_r;
  double inv_g = 1.0 / gamma_g;
  double inv_b = 1.0 / gamma_b;
  int skip_gamma = fabs(gamma_r - 1.0) < 0.001 &&
                   fabs(gamma_g - 1.0) < 0.001 &&
                   fabs(gamma_b - 1.0) < 0.001;
  int skip_shadow = shadow_lift <= 0.0001;

  for (int j = 0; j < gamma_size; j++) {
    double norm = (double)j / (double)(gamma_size - 1);
    double ramp_r, ramp_g, ramp_b;

    if (skip_gamma) {
      ramp_r = ramp_g = ramp_b = norm;
    } else {
      ramp_r = pow(norm, inv_r);
      ramp_g = pow(norm, inv_g);
      ramp_b = pow(norm, inv_b);
    }

    if (!skip_shadow) {
      ramp_r = apply_shadow_lift(ramp_r, shadow_lift);
      ramp_g = apply_shadow_lift(ramp_g, shadow_lift);
      ramp_b = apply_shadow_lift(ramp_b, shadow_lift);
    }

    ramp_r *= tr * brightness;
    ramp_g *= tg * brightness;
    ramp_b *= tb * brightness;

    red[j] = (uint16_t)clamp01(ramp_r * 65535.0, 0.0, 65535.0);
    green[j] = (uint16_t)clamp01(ramp_g * 65535.0, 0.0, 65535.0);
    blue[j] = (uint16_t)clamp01(ramp_b * 65535.0, 0.0, 65535.0);
  }
}
