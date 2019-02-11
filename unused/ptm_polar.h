#ifndef PTM_POLAR_H
#define PTM_POLAR_H

#include <stdint.h>
#include <stdbool.h>

namespace ptm {

int polar_decomposition_3x3(double* _A, bool right_sided, double* U, double* P);
void InnerProduct(double *A, int num, const double (*coords1)[3], double (*coords2)[3], int8_t* permutation);
int FastCalcRMSDAndRotation(double *A, double E0, double *p_nrmsdsq, double *q, double* U);

int FastCalcRMSD(double *A, double E0, double *p_nrmsdsq);
void increment_innerproduct(double *A, int i, double (*P)[3], double (*Q)[3], uint8_t* perm_P, uint8_t* perm_Q, double* p_G1, double* p_G2);
void decrement_innerproduct(double *A, int i, double (*P)[3], double (*Q)[3], uint8_t* perm_P, uint8_t* perm_Q, double* p_G1, double* p_G2);
void full_innerproduct(double *A, int num, double (*P)[3], double (*Q)[3], uint8_t* perm_P, uint8_t* perm_Q, double* p_G1, double* p_G2);

}

#endif
