//
// iirdesign.h - IIR filter of order up to 3 designed from a Laplace transfer function by the bilinear transform (used by the REAL pedals whose filters are given by their schematics).
//
#ifndef _iirdesign_h
#define _iirdesign_h
// IIR filter of order up to 3 from H(s) = (b0 + b1 s + b2 s^2 + b3 s^3) / (a0 + a1 s + a2 s^2 + a3 s^3) by the bilinear transform
struct TIir
{
	int n;
	double b[4], a[4], x[4], y[4];

	void Reset (void) { for (int i = 0; i < 4; i++) x[i] = y[i] = 0.0; }

	void Design (int order, const double *bs, const double *as, double fs)
	{
		n = order;
		const double c = 2.0 * fs;
		double nz[4] = {0, 0, 0, 0}, dz[4] = {0, 0, 0, 0};
		for (int k = 0; k <= n; k++)
		{
			// (1 - z^-1)^k (1 + z^-1)^(n - k)
			double p[4] = {1, 0, 0, 0};
			for (int i = 0; i < k; i++) { double q[4] = {0, 0, 0, 0}; for (int j = 0; j < 4; j++) { q[j] += p[j]; if (j + 1 < 4) q[j + 1] -= p[j]; } for (int j = 0; j < 4; j++) p[j] = q[j]; }
			for (int i = 0; i < n - k; i++) { double q[4] = {0, 0, 0, 0}; for (int j = 0; j < 4; j++) { q[j] += p[j]; if (j + 1 < 4) q[j + 1] += p[j]; } for (int j = 0; j < 4; j++) p[j] = q[j]; }
			double ck = 1.0;
			for (int i = 0; i < k; i++) ck *= c;
			for (int j = 0; j <= n; j++) { nz[j] += bs[k] * ck * p[j]; dz[j] += as[k] * ck * p[j]; }
		}
		for (int j = 0; j <= n; j++) { b[j] = nz[j] / dz[0]; a[j] = dz[j] / dz[0]; }
		Reset ();
	}

	double Run (double u)
	{
		for (int i = 3; i > 0; i--) { x[i] = x[i - 1]; y[i] = y[i - 1]; }
		x[0] = u;
		double s = 0.0;
		for (int j = 0; j <= n; j++) s += b[j] * x[j];
		for (int j = 1; j <= n; j++) s -= a[j] * y[j];
		y[0] = s;
		return s;
	}
};


#endif
