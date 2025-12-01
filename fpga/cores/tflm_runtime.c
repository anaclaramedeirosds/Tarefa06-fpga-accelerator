#include "tflm_runtime.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <math.h>

/* Modelo embutido (array binário) - símbolo renomeado */
extern unsigned char __ml_model_blob[];
extern unsigned int __ml_model_blob_len;

/* arquitetura esperada (do modelo usado)
 * input -> dense(16,relu) -> dense(16,relu) -> dense(1)
 */
#define NEURONS_L1 16
#define NEURONS_L2 16

/* ponteiros para seções de dados (apontados dentro do blob) */
static const int8_t *w1 = NULL;
static const int8_t *b1 = NULL;
static const int8_t *w2 = NULL;
static const int8_t *b2 = NULL;
static const int8_t *wout = NULL;
static const int8_t *bout = NULL;

static int initialized = 0;

/* utilitários */
static inline int8_t clamp_int8(int32_t v) {
    if (v > 127) return 127;
    if (v < -128) return -128;
    return (int8_t)v;
}
static inline int8_t relu_q(int32_t v) {
    if (v < 0) return 0;
    if (v > 127) return 127;
    return (int8_t)v;
}

/* segurança: verifica se um offset + tamanho cabem no blob */
static int check_bounds(unsigned int offset, unsigned int size) {
    if (offset + size > __ml_model_blob_len) return 0;
    return 1;
}

void inference_init(void) {
    if (initialized) {
        printf("[INF_DBG] inference_init: já inicializado\n");
        return;
    }

    printf("\n[INF_DBG] Inicializando TFLM runtime (debug)\n");
    printf("[INF_DBG] Blob size = %u bytes\n", __ml_model_blob_len);
    printf("[INF_DBG] Blob magic bytes: %02X %02X %02X %02X\n",
           __ml_model_blob[0], __ml_model_blob[1], __ml_model_blob[2], __ml_model_blob[3]);

    /* Offsets que eram usados (mantive como default) */
    const unsigned int off_w1   = 0xA8C; /* dense_2 weights (16 bytes) */
    const unsigned int off_b1   = 0xA9C; /* dense_2 biases  (16 bytes) */
    const unsigned int off_w2   = 0xAAC; /* dense_3 weights (256 bytes) */
    const unsigned int off_b2   = 0xBAC; /* dense_3 biases  (16 bytes) */
    const unsigned int off_wout = 0xBBC; /* dense_4 weights (16 bytes) */
    const unsigned int off_bout = 0xBCC; /* dense_4 bias    (1 byte)  */

    printf("[INF_DBG] Usando offsets (defaults): w1=0x%X b1=0x%X w2=0x%X b2=0x%X wout=0x%X bout=0x%X\n",
           off_w1, off_b1, off_w2, off_b2, off_wout, off_bout);

    /* checagens de limites */
    if (!check_bounds(off_w1, NEURONS_L1)) {
        printf("[INF_ERR] off_w1 fora do blob (off=%u size=%u)\n", off_w1, NEURONS_L1);
    } else {
        w1 = (const int8_t*)(__ml_model_blob + off_w1);
    }

    if (!check_bounds(off_b1, NEURONS_L1)) {
        printf("[INF_ERR] off_b1 fora do blob (off=%u size=%u)\n", off_b1, NEURONS_L1);
    } else {
        b1 = (const int8_t*)(__ml_model_blob + off_b1);
    }

    if (!check_bounds(off_w2, NEURONS_L2 * NEURONS_L1)) {
        printf("[INF_ERR] off_w2 fora do blob (off=%u size=%u)\n", off_w2, NEURONS_L2 * NEURONS_L1);
    } else {
        w2 = (const int8_t*)(__ml_model_blob + off_w2);
    }

    if (!check_bounds(off_b2, NEURONS_L2)) {
        printf("[INF_ERR] off_b2 fora do blob (off=%u size=%u)\n", off_b2, NEURONS_L2);
    } else {
        b2 = (const int8_t*)(__ml_model_blob + off_b2);
    }

    if (!check_bounds(off_wout, NEURONS_L2)) {
        printf("[INF_ERR] off_wout fora do blob (off=%u size=%u)\n", off_wout, NEURONS_L2);
    } else {
        wout = (const int8_t*)(__ml_model_blob + off_wout);
    }

    if (!check_bounds(off_bout, 1)) {
        printf("[INF_ERR] off_bout fora do blob (off=%u size=1)\n", off_bout);
    } else {
        bout = (const int8_t*)(__ml_model_blob + off_bout);
    }

    /* relatório resumido */
    if (w1 && b1 && w2 && b2 && wout && bout) {
        printf("[INF_DBG] Todos ponteiros iniciais atribuídos com sucesso.\n");
        printf("[INF_DBG] w1[0..3]=%d %d %d %d\n", w1[0], w1[1], w1[2], w1[3]);
        printf("[INF_DBG] b1[0..3]=%d %d %d %d\n", b1[0], b1[1], b1[2], b1[3]);
        printf("[INF_DBG] out_bias=%d\n", (int)(*bout));
        initialized = 1;
        printf("[INF_DBG] Modelo inicializado com sucesso.\n");
    } else {
        printf("[INF_WARN] Inicializacao incompleta: alguns ponteiros nao foram atribuídos.\n");
        printf("[INF_WARN] O loop de inferencia ira retornar 0.0 até que offsets sejam corrigidos.\n");
        /* não setamos initialized para 1 — o código de run verificará e ainda permitirá execução segura */
    }

    printf("[INF_DBG] FIM inference_init\n\n");
}

float inference_run(float x_value) {
    static int dbg_counter = 0;

    /* Se não inicializado corretamente, evita crash: retorna 0.0 e loga uma vez cada 100 iters */
    if (!(w1 && b1 && w2 && b2 && wout && bout)) {
        dbg_counter++;
        if ((dbg_counter % 100) == 1) {
            printf("[INF_RUN_WARN] inference_run chamado mas ponteiros de pesos invalidos. retornando 0.0\n");
        }
        return 0.0f;
    }

    /* quantiza entrada (mesma lógica de antes) */
    const float pi = 3.14159265f;
    float x_norm = x_value / (2.0f * pi);
    if (x_norm < 0.0f) x_norm = 0.0f;
    if (x_norm > 1.0f) x_norm = 1.0f;
    int8_t x_q = (int8_t)((x_norm * 255.0f) - 128.0f);

    /* Debug primario */
    if ((dbg_counter % 50) == 0) {
        printf("[INF_RUN_DBG] x=%.6f norm=%.6f quant=%d\n", x_value, x_norm, (int)x_q);
    }
    dbg_counter++;

    /* Layer1 */
    int8_t out_l1[NEURONS_L1];
    for (int i = 0; i < NEURONS_L1; i++) {
        int32_t acc = (int32_t)b1[i] * 8;
        acc += (int32_t)w1[i] * (int32_t)x_q;
        acc /= 32;
        out_l1[i] = relu_q(acc);
    }

    /* Layer2 */
    int8_t out_l2[NEURONS_L2];
    for (int i = 0; i < NEURONS_L2; i++) {
        int32_t acc = (int32_t)b2[i] * 8;
        for (int j = 0; j < NEURONS_L1; j++) {
            acc += (int32_t)w2[i * NEURONS_L1 + j] * (int32_t)out_l1[j];
        }
        acc /= 32;
        out_l2[i] = relu_q(acc);
    }

    /* Output */
    int32_t acc_out = (int32_t)(*bout) * 8;
    for (int i = 0; i < NEURONS_L2; i++) {
        acc_out += (int32_t)wout[i] * (int32_t)out_l2[i];
    }
    acc_out /= 32;
    int8_t out_q = clamp_int8(acc_out);

    const float out_scale = 0.0078125f; /* 1/128 */
    float out_f = (float)out_q * out_scale;
    if (out_f > 1.0f) out_f = 1.0f;
    if (out_f < -1.0f) out_f = -1.0f;

    /* debug ocasional da saida quantizada */
    if ((dbg_counter % 50) == 1) {
        printf("[INF_RUN_DBG] out_q=%d out_f=%.6f\n", (int)out_q, out_f);
    }

    return out_f;
}

unsigned char inference_output_to_led_pattern(float output_value) {
    float norm = (output_value + 1.0f) * 0.5f;
    if (norm < 0.0f) norm = 0.0f;
    if (norm > 1.0f) norm = 1.0f;
    return (unsigned char)(norm * 255.0f);
}
