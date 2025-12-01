#ifndef TFLM_RUNTIME_H
#define TFLM_RUNTIME_H

#include <stdint.h>

void inference_init(void);

/* input: valor x no intervalo [0, 2*PI]
 * retorno: aproximação de sin(x), no intervalo [-1, 1]
 */
float inference_run(float x);

/* converte saída float [-1,1] para um valor de 0..255 para LEDs */
unsigned char inference_output_to_led_pattern(float output_value);

#endif // TFLM_RUNTIME_H
