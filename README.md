# Tarefa06-fpga-accelerator

## Sobre o Projeto
Este projeto implementa um sistema embarcado que executa o modelo TensorFlow Lite Micro (TFLM) em uma FPGA ColorLight i9 utilizando um SoC LiteX com processador VexRiscv. O sistema realiza inferências periódicas do modelo "hello_world" e utiliza a saída para controlar LEDs, criando um efeito visual de barra progressiva.

## Objetivo
Desenvolver um acelerador hardware/software integrado que permita a execução de modelos de machine learning em sistemas embarcados com FPGA, utilizando as ferramentas LiteX para síntese e TensorFlow Lite Micro para inferência.

### Fluxo de dados
1. **Modelo ML** → Conversão para TFLite → Embedding em C
2. **SoC LiteX** → Carrega firmware → Inicializa TFLM
3. **Inferência** → Execução periódica → Saída senoidal
4. **Controle** → Mapeamento para LEDs → Efeito visual

## Estrutura do projeto
```bash
Tarefa06-fpga-accelerator/
├── 📁 fpga/
│   └── 📁 cores/
│       ├──  soc_color.py              # Geração do SoC LiteX
│       ├──  Makefile                  # Build do firmware
│       ├──  main_fw.c                 # Aplicação principal
│       ├──  tflm_runtime.c/h          # Port TFLM para LiteX
│       ├──  model_blob.c              # Modelo embedded em C
│       ├──  linker.ld                 # Linker script
│       └──  firmware.bin              # Firmware compilado
├── 📁 models/
│   ├──  model.keras                  # Modelo treinado
│   ├──  model.tflite                 # Modelo quantizado
│   └──  model_no_quant.tflite        # Modelo não quantizado
├──  hello_world_tflite_micro.ipynb   # Treinamento do modelo
└──  README.md
```

### Hardware
- **FPGA**: ColorLight i9 (revisão 7.2)
- **SoC**: LiteX com processador VexRiscv/PicoRV32
- **Memória**: RAM interna do SoC
- **Periféricos**: GPIO para controle de 8 LEDs

### Software
- **TensorFlow Lite Micro**: Runtime para microcontroladores
- **LiteX**: Framework para síntese de SoC
- **OSS CAD Suite**: Ferramentas EDA open-source (Yosys, NextPNR)

### Build e execução
1. Entrar no ambiente de desenvolvimento OSS-CAD-SUITE:
```bash
source [SEU-PATH]/oss-cad-suite/environment
```
2. Gerar/compilar o soc e o bitstream:
```bash
python3 fpga/cores/soc_color.py --board i9 --revision 7.2 --build --cpu-type=picorv32 --ecppack-compress
```
3. Entrar no diretório:
```bash
cd fpga/cores
```
4. Rodar o comando:
```bash
make
```
4. Retornar ao diretório anterior:
```bash
cd ..
```
6. Programar a FPGA:
```bash
sudo ~/[SEU-PATH]/oss-cad-suite/bin/openFPGALoader -v -b colorlight-i5 build/colorlight_i5/gateware/colorlight_i5.bit
```
7. Conectar via terminal serial (litex_term) e carregar o kernel:
```bash
litex_term /dev/ttyACM0 --kernel fpga/cores/firmware.bin
```
8. Se depois de rodar o comando acima e não aperecer nada, aperta ENTER e após aparecer litex> ou RUNTIME> é preciso digitar reboot e apertar enter.
