#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <uart.h>
#include <console.h>
#include <generated/csr.h>
#include <irq.h>

#include "tflm_runtime.h"

static char *read_line_nonblock(void)
{
    static char buffer[64];
    static int pos = 0;
    char ch;

    if (!readchar_nonblock())
        return NULL;

    ch = readchar();
    switch (ch) {
    case 0x7f:
    case 0x08:
        if (pos > 0) {
            pos--;
            putsnonl("\x08 \x08");
        }
        break;
    case '\r':
    case '\n':
        buffer[pos] = 0;
        putsnonl("\n");
        pos = 0;
        return buffer;
    default:
        if (pos < (int)sizeof(buffer) - 1) {
            buffer[pos++] = ch;
            /* substitui uso problemático de compound literal por buffer temporário */
            {
                char tmp[2] = { ch, 0 };
                putsnonl(tmp);
            }
        }
        break;
    }
    return NULL;
}

static char *pop_token(char **s)
{
    char *p = strchr(*s, ' ');
    if (!p) {
        char *t = *s;
        *s += strlen(*s);
        return t;
    }
    *p = 0;
    char *token = *s;
    *s = p + 1;
    return token;
}

static void print_prompt(void) {
    printf("RUNTIME> ");
}

static void cmd_help(void) {
    puts("Comandos disponiveis:");
    puts("  help    - mostra esta ajuda");
    puts("  reboot  - reinicia CPU");
    puts("  led     - inverte leds externos");
    puts("  execute - inicia testes e loop de inferencia");
}

static void do_reboot(void) {
    ctrl_reset_write(1);
}

static void invert_leds(void) {
    int v = leds_out_read();
    leds_out_write(~v);
    printf("LEDs invertidos (valor anterior: 0x%02X)\n", v & 0xFF);
}

/* Função principal de demonstração: roda testes de LED e o loop de inferência */
static void run_demo(void) {
    printf("Inicializando runtime de inferencia...\n");
    inference_init();

    /* Testes visuais nos 8 LEDs externos */
    printf("\n-- TESTE DE LEDS EXTERNOS --\n");
    printf("Teste A: barra crescente\n");
    for (int count = 0; count <= 8; count++) {
        unsigned char pat = 0;
        for (int i = 0; i < count; i++) pat |= (1 << i);
        leds_out_write(pat);
        printf("  %d/8 -> 0x%02X\n", count, pat);
        for (volatile int d = 0; d < 500000; d++);
    }

    printf("Teste B: efeito 'vaivem' (ida e volta)\n");
    for (int r = 0; r < 3; r++) {
        for (int p = 0; p < 8; p++) {
            leds_out_write(1 << p);
            for (volatile int d = 0; d < 300000; d++);
        }
        for (int p = 7; p >= 0; p--) {
            leds_out_write(1 << p);
            for (volatile int d = 0; d < 300000; d++);
        }
    }

    printf("Teste C: piscar todos\n");
    for (int b = 0; b < 5; b++) {
        leds_out_write(0xFF);
        for (volatile int d = 0; d < 400000; d++);
        leds_out_write(0x00);
        for (volatile int d = 0; d < 400000; d++);
    }
    printf("-- FIM TESTES LEDS --\n\n");

    printf("Entrando em loop de inferencia contínua (Ctrl+C para sair)\n");

    float x = 0.0f;
    const float step = 0.1f;
    const float pi = 3.14159265f;
    int iter = 0;

    while (1) {
        float y = inference_run(x);
        unsigned char led_val = inference_output_to_led_pattern(y);

        /* Converte led_val para barra de LEDs: acende N leds a partir do LSB */
        unsigned char out = 0;
        int leds_on = (int)((led_val / 255.0f) * 8.0f + 0.5f);
        if (leds_on < 0) leds_on = 0;
        if (leds_on > 8) leds_on = 8;
        for (int i = 0; i < leds_on; i++) out |= (1 << i);

        leds_out_write(out);

        if ((iter % 10) == 0) {
            int x_milli = (int)(x * 1000.0f);
            int y_milli = (int)(y * 1000.0f);
            printf("It %4d | x=%d.%03d | y=%d.%03d | led=%3u | bits=0x%02X (%d/8)\n",
                   iter,
                   x_milli/1000, x_milli%1000,
                   (y_milli<0? -y_milli: y_milli)/1000, (y_milli<0? -y_milli: y_milli)%1000,
                   led_val,
                   out,
                   leds_on);
        }

        x += step;
        if (x >= 2.0f * pi) {
            x = 0.0f;
            printf("\n--- Ciclo completado (0..2PI) ---\n\n");
        }
        iter++;

        /* pequeno atraso */
        for (volatile int i = 0; i < 250000; i++);

        /* checa se o usuário enviou Ctrl+C */
        if (readchar_nonblock()) {
            char c = readchar();
            if (c == 0x03) {
                printf("\nExecucao interrompida pelo usuario.\n");
                break;
            }
        }
    }
}

/* console service */
static void service_console(void) {
    char *line = read_line_nonblock();
    if (!line) return;
    char *tok = pop_token(&line);

    if (strcmp(tok, "help") == 0) cmd_help();
    else if (strcmp(tok, "reboot") == 0) do_reboot();
    else if (strcmp(tok, "led") == 0) invert_leds();
    else if (strcmp(tok, "execute") == 0) run_demo();
    else printf("Comando desconhecido: %s\n", tok);

    print_prompt();
}

/* main */
int main(void) {
#ifdef CONFIG_CPU_HAS_INTERRUPT
    irq_setmask(0);
    irq_setie(1);
#endif

    uart_init();
    printf("Hellorld! (custom firmware)\n");
    cmd_help();
    print_prompt();

    /* requisito: inicia automaticamente a execução ao ligar */
    run_demo();

    while (1) {
        service_console();
    }

    return 0;
}
