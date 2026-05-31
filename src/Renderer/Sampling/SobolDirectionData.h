/* Copyright (c) 2007 Massachusetts Institute of Technology
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 * 
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE
 * LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
 * OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
 * WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE. 
 */

#ifndef SOBOLSEQ_H
#define SOBOLSEQ_H

/* Data on the primitive binary polynomials (a) and the corresponding
   starting values m, for Sobol sequences in up to 1111 dimensions,
   taken from:
        P. Bratley and B. L. Fox, Algorithm 659, ACM Trans.
	Math. Soft. 14 (1), 88-100 (1988),
   as modified by:
        S. Joe and F. Y. Kuo, ACM Trans. Math. Soft 29 (1), 49-57 (2003). */

#define MAXDIM 1111
#define MAXDEG 12

#include <cstdint>

/* successive primitive binary-coefficient polynomials p(z)
   = a_0 + a_1 z + a_2 z^2 + ... a_31 z^31, where a_i is the 
     i-th bit of sobol_a[j] for the j-th polynomial. */
extern const uint32_t sobol_a[MAXDIM - 1];

/* starting direction #'s m[i] = sobol_minit[i][j] for i=0..d of the
 * degree-d primitive polynomial sobol_a[j]. */
extern const uint32_t sobol_minit[MAXDEG + 1][MAXDIM - 1];

#endif
