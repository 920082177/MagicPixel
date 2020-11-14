#ifndef __DEF_H_
#define __DEF_H_
#include <math.h>
#include <sys/param.h>
#include "esp_system.h"
#include "soc/soc.h"
#include "nvs_flash.h"
#include "esp_attr.h"
#include "esp_wpa2.h"
//#include "fftw3.h"

#define N   64      
#define LOG2N 6
#define PI  3.1415926535

typedef struct
{
	float real;
	float image;
}DFT_Complex;

void DFT_Cal(uint16_t *dft_in, float *dft_amp);
void Rader(uint16_t* arr,int n);
void Complex_Add(DFT_Complex A,DFT_Complex B,DFT_Complex* C);
void Complex_Sub(DFT_Complex A,DFT_Complex B,DFT_Complex* C);
void Complex_Mul(DFT_Complex A,DFT_Complex B,DFT_Complex* C);
void FFT_Cal(uint16_t* fft_in, float *amp);

#endif