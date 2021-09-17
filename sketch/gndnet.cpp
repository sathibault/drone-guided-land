#include <stdint.h>

////// Convolutional layer implementation
// co - num. outputs
// ci - num. inputs
// k - kernel size
// sx - horz. stride
// sy - stride from end of line to next line
// h0 - output height
// w0 - output width
// hh - input height
// ww - input width
// imem - input activations
// omem - output activations
// coeff - kernel weights

static void conv2d(int co, int ci, int k, int sx, int sy,
		   int h0, int w0, int hh, int ww,
		   int8_t *imem, int16_t *omem, int8_t *coeff) {
  int sz = ww*hh;

  bool accum;
  int16_t prod, w;
  uint32_t basei, addri, baseo, addro, addrw;

  addrw = 0;
  baseo = 0;
  for (int fo = 0; fo < co; fo++) {
    basei = 0;
    accum = false;
    for (int fi = 0; fi < ci; fi++) {
      basei = fi*sz;
      for (int ky = 0; ky < k; ky++) {
	for (int kx = 0; kx < k; kx++) {
	  addri = basei+kx;
	  addro = baseo;
	  w = coeff[addrw++];
	  for (int iy = 0; iy < h0; iy++) {
	    for (int ix = 0; ix < w0; ix++) {
	      prod = (int16_t)imem[addri] * w;
	      addri += sx;
	      if (accum)
		omem[addro] += prod;
	      else
		omem[addro] = prod;
	      addro += 1;
	    }
	    addri += sy;
	  }
	  accum = true;
	}
	basei += ww;
      }
    }
    baseo = addro;
  }
}

////// Combined bias/relu layer implementation
// ci - num. inputs
// h0 - output height
// w0 - output width
// imem - input activations
// omem - output activations
// bias - output biases
// scale - output scale

static void relu(int ci, int h0, int w0,
		 int16_t *imem, int8_t *omem, int16_t *bias, int8_t *scale) {
  int8_t sc;
  int16_t b,rd;
  uint32_t addrc,addrio;
  
  addrio = 0;
  for (int fi = 0; fi < ci; fi++) {
    b = bias[fi];
    sc = scale[fi];
    for (int iy = 0; iy < h0; iy++) {
      for (int ix = 0; ix < w0; ix++) {
	rd = (imem[addrio] + b) >> sc;
	omem[addrio] = (rd >= 0) ? rd : 0;
	addrio += 1;
      }
    }
  }
}

// Include network weights (and biases)
#include "wts.h"

// 2-Layer fully-convolutional neural network
// Input: (3,80,80) image: RGB
// Output: (3,36,36) Activation: non-pavement, pavement, unknown
// maps - 48KB memory buffer with input image at offset 0

void gndnet(uint8_t *maps) {
  // memory map:
  // 0:19200 (3,80,80,int8)
  // 19200:48080 (10,38,38,int16)
  conv2d(10, 3, 5, 2, 84, 38, 38, 80, 80,
	 (int8_t *)(maps+0), (int16_t *)(maps+19200), weights);
  relu(10, 38, 38,
       (int16_t *)(maps+19200), (int8_t *)(maps+0), biases, weights+750);
  
  // 0:14440 (10,38,38,int8)
  // 14440:22216 (3,36,36,int16)
  conv2d(3, 10, 3, 1, 2, 36, 36, 38, 38,
	 (int8_t *)(maps+0), (int16_t *)(maps+14440), weights+760);
  relu(3, 36, 36,
       (int16_t *)(maps+14440), (int8_t *)(maps+0), biases+10, weights+1030);
}
