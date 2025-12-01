from migen import *
from litex.gen import *
from litex.build.io import DDROutput
from litex_boards.platforms import colorlight_i5

# LiteX cores / helpers
from litex.soc.cores.clock import ECP5PLL
from litex.soc.integration.soc_core import SoCCore
from litex.soc.integration.builder import Builder
from litex.soc.cores.led import LedChaser
from litex.build.generic_platform import Pins, IOStandard
from litex.soc.cores.gpio import GPIOOut
from litedram.modules import M12L64322A
from litedram.phy import GENSDRPHY, HalfRateGENSDRPHY

# --------------------------------------------------------------------
# Clock / Reset generator (refatorado)
# --------------------------------------------------------------------
class CRGModule(LiteXModule):
    def __init__(self, platform, sys_clk_freq, use_internal_osc=False,
                 enable_usb_pll=False, enable_video_pll=False, sdram_rate="1:1"):
        self.reset_local = Signal()
        self.cd_sys = ClockDomain()
        # depending on SDRAM rate create extra domains
        if sdram_rate == "1:2":
            self.cd_sys2x = ClockDomain()
            self.cd_sys2x_ps = ClockDomain()
        else:
            self.cd_sys_ps = ClockDomain()

        # --- clock source selection ---
        if not use_internal_osc:
            clk_pin = platform.request("clk25")
            clk_in_freq = 25e6
        else:
            clk_pin = Signal()
            div = 5
            self.specials += Instance("OSCG",
                p_DIV=div,
                o_OSC=clk_pin
            )
            clk_in_freq = 310e6/div

        rst_n = platform.request("cpu_reset_n")

        # --- primary PLL ---
        self.pll = top_pll = ECP5PLL()
        self.comb += top_pll.reset.eq(~rst_n | self.reset_local)
        top_pll.register_clkin(clk_pin, clk_in_freq)
        top_pll.create_clkout(self.cd_sys, sys_clk_freq)
        if sdram_rate == "1:2":
            top_pll.create_clkout(self.cd_sys2x, 2*sys_clk_freq)
            # use phase 180 for ps domain (empirical)
            top_pll.create_clkout(self.cd_sys2x_ps, 2*sys_clk_freq, phase=180)
        else:
            top_pll.create_clkout(self.cd_sys_ps, sys_clk_freq, phase=180)

        # optional USB PLL
        if enable_usb_pll:
            self.usb_pll = usb_pll = ECP5PLL()
            self.comb += usb_pll.reset.eq(~rst_n | self.reset_local)
            usb_pll.register_clkin(clk_pin, clk_in_freq)
            self.cd_usb_12 = ClockDomain()
            self.cd_usb_48 = ClockDomain()
            usb_pll.create_clkout(self.cd_usb_12, 12e6, margin=0)
            usb_pll.create_clkout(self.cd_usb_48, 48e6, margin=0)

        # optional video PLL
        if enable_video_pll:
            self.video_pll = vpll = ECP5PLL()
            self.comb += vpll.reset.eq(~rst_n | self.reset_local)
            vpll.register_clkin(clk_pin, clk_in_freq)
            self.cd_hdmi = ClockDomain()
            self.cd_hdmi5x = ClockDomain()
            vpll.create_clkout(self.cd_hdmi, 40e6, margin=0)
            vpll.create_clkout(self.cd_hdmi5x, 200e6, margin=0)

        # SDRAM physical clock to board pin (DDR output)
        sdram_clk = ClockSignal("sys2x_ps" if sdram_rate == "1:2" else "sys_ps")
        self.specials += DDROutput(1, 0, platform.request("sdram_clock"), sdram_clk)


# --------------------------------------------------------------------
# SoC principal (refatorado)
# --------------------------------------------------------------------
class ColorlightSoC(SoCCore):
    def __init__(self, board="i5", revision="7.0", toolchain="trellis",
                 sys_clk_freq=60e6, with_led_chaser=False,
                 use_internal_osc=False, sdram_rate="1:1",
                 with_video_terminal=False, with_video_framebuffer=False,
                 **kwargs):

        board = board.lower()
        assert board in ["i5", "i9"]
        platform = colorlight_i5.Platform(board=board, revision=revision, toolchain=toolchain)

        # CRG: detecta se UART usa usb_acm e se vídeo será usado
        usb_needed = kwargs.get("uart_name", None) == "usb_acm"
        video_needed = with_video_terminal or with_video_framebuffer

        self.crg = CRGModule(platform, sys_clk_freq,
                             use_internal_osc=use_internal_osc,
                             enable_usb_pll=usb_needed,
                             enable_video_pll=video_needed,
                             sdram_rate=sdram_rate)

        # SoC base
        SoCCore.__init__(self, platform, int(sys_clk_freq),
                         ident="LiteX SoC on Colorlight " + board.upper(), **kwargs)

        # SPI flash selection por placa
        if board == "i5":
            from litespi.modules import GD25Q16 as SpiFlashModule
        else:
            from litespi.modules import W25Q64 as SpiFlashModule

        from litespi.opcodes import SpiNorFlashOpCodes as Codes
        self.add_spi_flash(mode="1x", module=SpiFlashModule(Codes.READ_1_1_1))

        # SDRAM (adiciona se não houver RAM integrada)
        if not self.integrated_main_ram_size:
            sdrphy_cls = HalfRateGENSDRPHY if sdram_rate == "1:2" else GENSDRPHY
            self.sdrphy = sdrphy_cls(platform.request("sdram"))
            self.add_sdram("sdram",
                           phy=self.sdrphy,
                           module=M12L64322A(sys_clk_freq, sdram_rate),
                           l2_cache_size=kwargs.get("l2_size", 8192))

        # Custom 8-bit LED bank via extensão de pinos
        leds_pads = [
            ("leds_ext", 0,
             Pins("P17 P18 N18 L20 L18 G20 M18 N17"),
             IOStandard("LVCMOS33"))
        ]
        platform.add_extension(leds_pads)

        # substitui controle padrão por GPIOOut (8 bits)
        self.submodules.leds = GPIOOut(platform.request("leds_ext"))
        self.add_csr("leds")


# --------------------------------------------------------------------
# CLI entrypoint (praticidade para build)
# --------------------------------------------------------------------
def main():
    from litex.build.parser import LiteXArgumentParser
    parser = LiteXArgumentParser(platform=colorlight_i5.Platform, description="LiteX SoC for Colorlight (refactor).")
    parser.add_target_argument("--board", default="i5", help="Board type (i5 or i9).")
    parser.add_target_argument("--revision", default="7.0", help="Board revision.")
    parser.add_target_argument("--sys-clk-freq", default=60e6, type=float, help="System clock frequency.")
    ethopts = parser.target_group.add_mutually_exclusive_group()
    ethopts.add_argument("--with-ethernet", action="store_true", help="Enable Ethernet.")
    ethopts.add_argument("--with-etherbone", action="store_true", help="Enable Etherbone.")
    parser.add_target_argument("--remote-ip", default="192.168.1.100", help="Remote TFTP server IP.")
    parser.add_target_argument("--local-ip", default="192.168.1.50", help="Local IP.")
    sdopts = parser.target_group.add_mutually_exclusive_group()
    sdopts.add_argument("--with-spi-sdcard", action="store_true", help="Enable SPI SDCard.")
    sdopts.add_argument("--with-sdcard", action="store_true", help="Enable SDCard.")
    parser.add_target_argument("--eth-phy", default=0, type=int, help="Ethernet PHY index.")
    parser.add_target_argument("--use-internal-osc", action="store_true", help="Use internal oscillator.")
    parser.add_target_argument("--sdram-rate", default="1:1", help="SDRAM rate (1:1 or 1:2).")
    viopts = parser.target_group.add_mutually_exclusive_group()
    viopts.add_argument("--with-video-terminal", action="store_true", help="Enable HDMI terminal.")
    viopts.add_argument("--with-video-framebuffer", action="store_true", help="Enable HDMI framebuffer.")
    parser.add_target_argument("--with-lora", action="store_true", help="Enable SPI for LoRa module.")
    parser.add_target_argument("--with-aht10", action="store_true", help="Enable I2C for AHT10 sensor.")
    parser.add_target_argument("--use-example-pins", action="store_true", help="Load example pin file.")

    args = parser.parse_args()

    soc = ColorlightSoC(board=args.board, revision=args.revision,
                        toolchain=args.toolchain,
                        sys_clk_freq=args.sys_clk_freq,
                        with_ethernet=args.with_ethernet,
                        sdram_rate=args.sdram_rate,
                        with_etherbone=args.with_etherbone,
                        local_ip=args.local_ip,
                        remote_ip=args.remote_ip,
                        eth_phy=args.eth_phy,
                        use_internal_osc=args.use_internal_osc,
                        with_video_terminal=args.with_video_terminal,
                        with_video_framebuffer=args.with_video_framebuffer,
                        with_lora_spi=args.with_lora,
                        with_i2c_aht10=args.with_aht10,
                        use_example_pins=args.use_example_pins,
                        **parser.soc_argdict)

    soc.platform.add_extension(colorlight_i5._sdcard_pmod_io)

    if args.with_spi_sdcard:
        soc.add_spi_sdcard()
    if args.with_sdcard:
        soc.add_sdcard()

    builder = Builder(soc, **parser.builder_argdict)
    if args.build:
        builder.build(**parser.toolchain_argdict)

    if args.load:
        programmer = soc.platform.create_programmer()
        programmer.load_bitstream(builder.get_bitstream_filename(mode="sram"))

if __name__ == "__main__":
    main()
