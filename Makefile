MAKEFLAGS+=w
PYTHON=uv run
QUARTUS_DIR = C:/intelFPGA_lite/17.0/quartus/bin64
PROJECT = Arcade-IGSPGM
CONFIG = Arcade-IGSPGM
MISTER = root@mister-dev
OUTDIR = output_files
MAME_XML=util/mame.xml
RELEASES_DIR=releases

# Use wsl for submakes on windows
ifeq ($(OS),Windows_NT)
MAKE = wsl make
endif

RBF = $(OUTDIR)/$(CONFIG).rbf

rwildcard=$(foreach d,$(wildcard $(1:=/*)),$(call rwildcard,$d,$2) $(filter $(subst *,%,$2),$d))

SRCS_FULL = \
	$(call rwildcard,sys,*.v *.sv *.vhd *.vhdl *.qip *.sdc) \
	$(call rwildcard,rtl,*.v *.sv *.vhd *.vhdl *.qip *.sdc) \
	$(wildcard *.sdc *.v *.sv *.vhd *.vhdl *.qip)

SRCS = $(filter-out %_auto_ss.sv,$(SRCS_FULL))

$(OUTDIR)/Arcade-IGSPGM-Fast.rbf: $(SRCS)
	$(QUARTUS_DIR)/quartus_sh --flow compile $(PROJECT) -c Arcade-IGSPGM-Fast

$(OUTDIR)/Arcade-IGSPGM.rbf: $(SRCS)
	$(QUARTUS_DIR)/quartus_sh --flow compile $(PROJECT) -c Arcade-IGSPGM

deploy.done: $(RBF)
	scp $(RBF) $(MISTER):/media/fat/_Development/cores/IGSPGM.rbf
	echo done > deploy.done

deploy: deploy.done


mister/%: releases/% deploy.done
	scp "$<" $(MISTER):/media/fat/_Development/
	ssh $(MISTER) "echo 'load_core _Development/$(notdir $<)' > /dev/MiSTer_cmd"

mister: mister/PGM.mra
mister/finalb: mister/Final\ Blow\ (World).mra
mister/dinorex: mister/Dino\ Rex\ (World).mra
mister/qjinsei: mister/Quiz\ Jinsei\ Gekijoh\ (Japan).mra


rbf: $(OUTDIR)/$(CONFIG).rbf

sim:
	$(MAKE) -j8 -C sim sim

sim/run: sim/pgm

sim/test:
	$(MAKE) -j8 -C testroms TARGET=pgm_test
	$(MAKE) -j8 -C sim run GAME=pgm_test

sim/%:
	$(MAKE) -j8 -C sim run GAME=$*


debug: debug/pgm

debug/%:
	$(MAKE) -j8 -C testroms debug TARGET=$*


picorom: picorom/pgm_test

picorom/%:
	$(MAKE) -j8 -C testroms picorom TARGET=$*



rtl/jt10_auto_ss.sv:
	$(PYTHON) util/state_module.py --generate-csv docs/jt10_mapping.csv jt10 rtl/jt10_auto_ss.sv rtl/jt12/jt49/hdl/*.v rtl/jt12/hdl/adpcm/*.v rtl/jt12/hdl/*.v rtl/jt12/hdl/mixer/*.v

rtl/tv80_auto_ss.sv:
	$(PYTHON) util/state_module.py --generate-csv docs/tv80_mapping.csv tv80s rtl/tv80_auto_ss.sv rtl/tv80/*.v

rtl/fx68k_auto_ss.sv:
	$(PYTHON) util/state_module.py --generate-csv docs/fx68k_mapping.csv fx68k rtl/fx68k_auto_ss.sv rtl/fx68k/hdl/*.v

releases_clean:
	$(PYTHON) util/mame2mra.py --generate --all-machines --output releases_clean --config util/mame2mra.toml util/mame.xml

releases:
	$(PYTHON) util/mame2mra.py --generate --all-machines --output releases --config util/mame2mra.toml util/mame.xml
	patch -E -d releases -l -p1 -r - < releases.patch

releases.patch:
	diff -ruN -x "*.rbf" -x ".DS_Store" releases_clean releases > releases.patch || true

.PHONY: sim sim/run sim/test mister debug picorom rtl/jt10_auto_ss.sv rtl/tv80_auto_ss.sv releases releases_clean releases.patch
